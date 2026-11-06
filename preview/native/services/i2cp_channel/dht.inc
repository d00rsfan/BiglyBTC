// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. Explicit legacy17/raw18 DHT port-pair scope.
  static constexpr std::size_t DhtQueueLimit=8,DhtByteLimit=8*MaximumDhtMuxBytes;
  struct DhtWrite {std::uint64_t ticket=0,generation=0;Clock::time_point end{};Bytes frame;};
  std::uint16_t dhtQueryPort=0;std::uint64_t nextDhtTicket=1,dhtEpoch=0,nextDhtEpoch=1;
  std::deque<DhtWrite> dhtWrites;std::deque<DhtDispatch> dhtDispatches;
  std::deque<DhtDatagram> dhtReceived;std::size_t dhtBytes=0;bool dhtQueued=false;std::uint64_t dhtReceiveExpired=0;
  bool dhtReceives()const{return dhtQueryPort&&receiverCurrent()&&(!sourceAuthority||sourceAuthority->current());}
  void notifyDht(){
    if(dhtQueued){return;}dhtQueued=true;bool ok=false;
    try{const QPointer<InitialChannel> owner(q);const auto at=generation;ok=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* s=owner->impl_.get();if(s->generation!=at){return;}s->dhtQueued=false;if(s->active()){emit owner->dhtChanged();}},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){dhtQueued=false;fail(Failure::Resources);}
  }
  void clearDht(){dhtTimer.stop();dhtQueryPort=0;dhtEpoch=0;dhtWrites.clear();dhtDispatches.clear();dhtReceived.clear();dhtBytes=0;dhtQueued=false;dhtReceiveExpired=0;}
  void dhtDeadline(){
    if(!dhtQueryPort){return;}
    const auto now=Clock::now();
    bool changed=false;
    for(auto it=dhtWrites.begin();it!=dhtWrites.end();){
      if(it->generation!=generation||!dhtReceives()||now>=it->end){dhtDispatches.push_back({it->ticket,false});it=dhtWrites.erase(it);changed=true;}else{++it;}
    }
    for(auto it=dhtReceived.begin();it!=dhtReceived.end();){
      if(now>=it->end){if(dhtReceiveExpired!=std::numeric_limits<std::uint64_t>::max()){++dhtReceiveExpired;}dhtBytes-=it->payload.size()+(it->sender?it->sender->wire.size()+it->sender->signingPublicKey.size()+32:0);it=dhtReceived.erase(it);changed=true;}else{++it;}
    }
    if(changed){notifyDht();}if((dhtWrites.empty()&&dhtReceived.empty())||!active()){dhtTimer.stop();return;}
    auto end=Clock::time_point::max();for(const auto& w:dhtWrites){end=std::min(end,w.end);}for(const auto& received:dhtReceived){end=std::min(end,received.end);}
    dhtTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count())));
  }
  void flushDhtWrite(){
    dhtDeadline();if(dhtWrites.empty()||socket.bytesToWrite()!=0||state!=State::SessionCreated||!dhtReceives()){return;}
    if(!effectReady()){return;} // Fresh worker-held source permit, no FS on Qt.
    if(dhtWrites.empty()||!dhtReceives()){return;}
    auto& item=dhtWrites.front();if(item.generation!=generation||Clock::now()>=item.end){dhtDeadline();return;}
    // No callbacks or allocating preparation between the final fences and write.
    const auto at=generation,ticket=item.ticket;const auto size=static_cast<qint64>(item.frame.size());
    const auto written=socket.write(reinterpret_cast<const char*>(item.frame.data()),size);
    if(!current(at)||dhtWrites.empty()||dhtWrites.front().ticket!=ticket){return;}
    if(written!=size){fail(Failure::WriteFailed);return;}
    dhtWrites.pop_front();dhtDispatches.push_back({ticket,true});dhtDeadline();notifyDht();
  }
  bool enableDht(std::uint16_t port){
    if(!onThread()||dhtQueryPort||nextDhtEpoch==std::numeric_limits<std::uint64_t>::max()||port<1||port>=65535||port==TrackerLocalPort||port+1==TrackerLocalPort||state!=State::SessionCreated||!leaseCurrent()){return false;}
    dhtEpoch=nextDhtEpoch++;dhtQueryPort=port;notifyDht();return dhtQueryPort&&active();
  }
  void disableDht(){
    if(!onThread()){return;}clearDht();if(fetch&&!receiverEnabled&&!trackerReceives()&&!streamReceives()){cancelFetch(ReceiveDrop::Cancelled);}notifyDht();
  }
  ConfigResult<std::uint64_t> sendDht(View recipient,View payload,bool repliable,std::uint16_t port,unsigned timeout,std::optional<Clock::time_point> absoluteEnd={}){
    using Code=::biglybtc::services::i2p::ErrorCode;
    const auto reject=[](Code c,const char* why)->ConfigResult<std::uint64_t>{return std::unexpected(::biglybtc::services::i2p::Error{c,why});};
    const auto end=std::min(Clock::now()+std::chrono::milliseconds(std::min(timeout,30000U)),absoluteEnd.value_or(Clock::time_point::max()));
    if(!onThread()||!dhtReceives()||!port||!timeout||timeout>30000||payload.size()>10*1024){return reject(Code::InvalidFormat,"Explicit current bounded DHT scope required");}
    // Tickets are Native-only monotonic IDs, never the 32-bit I2CP nonce.
    // Reserve UINT64_MAX so postincrement cannot wrap/reuse a retired ID.
    if(dhtWrites.size()+dhtDispatches.size()==DhtQueueLimit||nextDhtTicket==std::numeric_limits<std::uint64_t>::max()){return reject(Code::TooLarge,"DHT queue or ticket identifier capacity exhausted");}
    try{
      const auto at=generation;auto destination=decodeDestination(recipient);if(!destination){return std::unexpected(destination.error());}
      auto data=repliable?encode1(leaseSigner->destination()->wire,leaseSigner->signingPrivateKey(),payload):ConfigResult<Bytes>{Bytes(payload.begin(),payload.end())};
      if(!data){return std::unexpected(data.error());}
      auto mux=encodeDhtMuxDatagram({Byte(repliable?17:18),dhtQueryPort,port,std::move(*data)},GzipMode::Deflate);const auto now=observedNow();
      if(!mux||!now||*now>0xffffffffffffULL-60000){return reject(Code::InvalidFormat,"DHT mux/expiration invalid");}
      auto frame=encodeDhtSendDatagram({*session,std::move(*destination),std::move(*mux),0,*now+60000,!repliable});if(!frame){return std::unexpected(frame.error());}
      if(!current(at)||!dhtReceives()||Clock::now()>=end){return reject(Code::InvalidFormat,"DHT preparation lost current source/lease/deadline");}
      const auto ticket=nextDhtTicket++;dhtWrites.push_back({ticket,at,end,std::move(*frame)});dhtDeadline();flushDhtWrite();if(!current(at)||!dhtQueryPort){return reject(Code::InvalidFormat,"DHT transport lost before dispatch");}return ticket;
    }catch(...){fail(Failure::Resources);return reject(Code::CryptoFailure,"DHT queue resources unavailable");}
  }
  bool cancelDht(std::uint64_t ticket){
    if(!onThread()){return false;}for(auto it=dhtWrites.begin();it!=dhtWrites.end();++it){if(it->ticket==ticket){dhtWrites.erase(it);dhtDispatches.push_back({ticket,false});dhtDeadline();notifyDht();return true;}}return false;
  }
  // Returns false for other scopes, preserving the tracker's exact raw route.
  bool observeDhtPayload(View payload,std::optional<Clock::time_point> fetchEnd,std::uint32_t messageId){
    const auto arrivalEnd=std::min(Clock::now()+std::chrono::seconds(30),fetchEnd.value_or(Clock::time_point::max()));
    if(!dhtQueryPort||payload.size()<10){return false;}
    const auto protocol=payload[9];const auto port=std::uint16_t((unsigned(payload[6])<<8)|payload[7]);
    if(!((protocol==17&&port==dhtQueryPort)||(protocol==18&&port==dhtQueryPort+1))){return false;}
    if(!dhtReceives()||(fetchEnd&&Clock::now()>=*fetchEnd)){dropReceive(ReceiveDrop::LeaseExpired);return true;}
    const auto at=generation;auto mux=decodeDhtMuxDatagram(payload);if(!mux||(protocol==17&&(!mux->fromPort||mux->fromPort>=65535))){dropReceive(ReceiveDrop::Malformed);return true;}
    DhtDatagram data;data.end=arrivalEnd;data.messageId=messageId;data.fromPort=mux->fromPort;data.toPort=mux->toPort;
    if(protocol==17){auto packet=decode1(mux->datagram);if(!packet){dropReceive(ReceiveDrop::InvalidDatagram);return true;}data.sender=std::move(packet->sender);data.payload=std::move(packet->payload);}
    else{data.payload=std::move(mux->datagram);}
    if(data.payload.size()>10*1024){dropReceive(ReceiveDrop::Malformed);return true;}
    const auto weight=data.payload.size()+(data.sender?data.sender->wire.size()+data.sender->signingPublicKey.size()+32:0);
    if(!current(at)||!dhtReceives()||Clock::now()>=arrivalEnd){dropReceive(ReceiveDrop::LeaseExpired);return true;}
    if(dhtReceived.size()==DhtQueueLimit||weight>DhtByteLimit-dhtBytes){dropReceive(ReceiveDrop::QueueFull);return true;}
    dhtBytes+=weight;dhtReceived.push_back(std::move(data));dhtDeadline();notifyDht();return true;
  }
