// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. One owned stream, no listener/backend thread.
  static constexpr std::size_t ReceiveQueueLimit=4,ReceiveByteLimit=4*MaximumMuxDatagramBytes,ReceiveControlLimit=4,ReceiveRecentLimit=64;
  struct Fetch {std::uint16_t sessionId=0;std::uint32_t messageId=0,size=0;std::uint64_t generation=0;Clock::time_point end{};bool begun=false;};
  struct QueuedControl {ReceiveControl value;std::uint64_t generation=0;};
  bool receiverEnabled=false,fastReceive=false,pendingFastReceive=false,receiveNotificationQueued=false,controlFlushing=false,controlLost=false;
  std::deque<ReceivedDatagram> received;
  std::size_t receiveBytes=0,controlCount=0,recentCount=0,recentPosition=0;
  std::array<QueuedControl,ReceiveControlLimit> receiveControls{};
  std::array<std::uint32_t,ReceiveRecentLimit> recentReceived{};
  std::optional<Fetch> fetch;
  std::uint64_t receiveDrops=0;std::optional<ReceiveDrop> lastReceiveDrop;
  std::size_t receiveWeight(const ReceivedDatagram& d)const{return std::visit([](const auto& body)->std::size_t{if constexpr(requires{body.sender;}){return body.sender.wire.size()+body.sender.signingPublicKey.size()+body.sender.hash.size()+body.payload.size();}else{return body.claimedSender.size()+body.payload.size();}},d.body);}
  ReceiverObservation receiveSnapshot()const noexcept{return {receiverEnabled,fastReceive,fetch.has_value(),controlLost,received.size(),receiveBytes,controlCount,receiveDrops,lastReceiveDrop};}
  void clearReceive(){receiveNotificationQueued=false;received.clear();receiveBytes=0;fetch.reset();receiveTimer.stop();receiverEnabled=false;fastReceive=false;pendingFastReceive=false;controlCount=0;recentCount=0;recentPosition=0;controlFlushing=false;controlLost=false;receiveDrops=0;lastReceiveDrop.reset();}
  bool receiverCurrent()const{return (state==State::SessionCreated||state==State::Reconfiguring)&&leaseCurrent();}
  bool remembered(std::uint32_t id)const{return std::any_of(received.begin(),received.end(),[id](const auto& d){return d.messageId==id;})||std::find(recentReceived.begin(),recentReceived.begin()+static_cast<std::ptrdiff_t>(recentCount),id)!=recentReceived.begin()+static_cast<std::ptrdiff_t>(recentCount);}
  void remember(std::uint32_t id){if(remembered(id)){return;}recentReceived[recentPosition]=id;recentPosition=(recentPosition+1)%ReceiveRecentLimit;recentCount=std::min(recentCount+1,ReceiveRecentLimit);}
  void notifyReceive(){
    if(receiveNotificationQueued){return;}receiveNotificationQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);const auto at=generation;queued=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* self=owner->impl_.get();if(self->generation!=at){return;}self->receiveNotificationQueued=false;if(self->active()&&self->session&&self->socket.state()==QAbstractSocket::ConnectedState){emit owner->receiverChanged();}},Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){receiveNotificationQueued=false;fail(Failure::Resources);}
  }
  void dropReceive(ReceiveDrop reason){if(receiveDrops!=std::numeric_limits<std::uint64_t>::max()){++receiveDrops;}lastReceiveDrop=reason;notifyReceive();}
  void removeBegin(std::uint32_t id){for(std::size_t i=0;i<controlCount;){if(receiveControls[i].value.begin&&receiveControls[i].value.messageId==id){for(std::size_t j=i+1;j<controlCount;++j){receiveControls[j-1]=receiveControls[j];}--controlCount;}else{++i;}}}
  void popControl(){for(std::size_t i=1;i<controlCount;++i){receiveControls[i-1]=receiveControls[i];}--controlCount;}
  bool enqueueControl(bool begin,std::uint32_t id){
    if(!session||!active()||socket.state()!=QAbstractSocket::ConnectedState){return false;}
    for(std::size_t i=0;i<controlCount;++i){if(receiveControls[i].generation==generation&&receiveControls[i].value.sessionId==*session&&receiveControls[i].value.messageId==id&&receiveControls[i].value.begin==begin){return true;}}
    if(controlCount==ReceiveControlLimit){controlLost=true;lastReceiveDrop=ReceiveDrop::ControlBackpressure;notifyReceive();return false;}
    receiveControls[controlCount++]={ReceiveControl{*session,id,begin},generation};flushReceiveControls();return active();
  }
  void cancelFetch(ReceiveDrop reason,bool release=true){
    if(!fetch){return;}const auto id=fetch->messageId;removeBegin(id);fetch.reset();receiveTimer.stop();remember(id);if(release){(void)enqueueControl(false,id);}dropReceive(reason);
  }
  void receiveDeadline(){
    if(!fetch){return;}if(!receiverEnabled&&!(trackerReceives()||streamReceives())){cancelFetch(ReceiveDrop::Cancelled);return;}if(!receiverCurrent()){cancelFetch(ReceiveDrop::LeaseExpired);return;}
    if(Clock::now()>=fetch->end){cancelFetch(ReceiveDrop::PendingTimeout);return;}const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(fetch->end-Clock::now()).count();receiveTimer.start(static_cast<int>(std::max<std::int64_t>(1,left)));
  }
  void flushReceiveControls(){
    if(controlFlushing||!active()){return;}if(controlCount&&!effectReady()){return;}controlFlushing=true;
    try{
      for(std::size_t steps=0;steps<2*ReceiveControlLimit&&controlCount&&active()&&socket.bytesToWrite()==0;++steps){
        const auto next=receiveControls[0];
        if(next.generation!=generation||!session||next.value.sessionId!=*session){popControl();continue;}
        if(next.value.begin){
          if(!fetch||fetch->messageId!=next.value.messageId||fetch->generation!=generation){popControl();continue;}
          if((!receiverEnabled&&!(trackerReceives()||streamReceives()))||!receiverCurrent()||Clock::now()>=fetch->end){cancelFetch((!receiverEnabled&&!(trackerReceives()||streamReceives()))?ReceiveDrop::Cancelled:!receiverCurrent()?ReceiveDrop::LeaseExpired:ReceiveDrop::PendingTimeout);continue;}
        }
        auto encoded=encodeReceiveControl(next.value);if(!encoded){fail(Failure::Resources);break;}const auto at=generation;
        if(!effectReady()){break;}
        const auto written=socket.write(reinterpret_cast<const char*>(encoded->data()),static_cast<qint64>(encoded->size()));
        if(!current(at)){break;}if(written!=static_cast<qint64>(encoded->size())){fail(Failure::WriteFailed);break;}
        popControl();if(next.value.begin&&fetch&&fetch->messageId==next.value.messageId){fetch->begun=true;}
      }
    }catch(...){controlFlushing=false;fail(Failure::Resources);return;}
    controlFlushing=false;
  }
  void availableDatagram(const MessageStatus& status){
    if(!session||status.sessionId!=*session){dropReceive(ReceiveDrop::WrongSession);return;}
    if(fastReceive){remember(status.messageId);dropReceive(ReceiveDrop::UnexpectedPayload);return;} // No legacy ACKs in immutable fast mode.
    if(fetch&&fetch->messageId==status.messageId){dropReceive(ReceiveDrop::Duplicate);return;} // Don't release a live fetch.
    if(remembered(status.messageId)){(void)enqueueControl(false,status.messageId);dropReceive(ReceiveDrop::Duplicate);return;}
    if((!receiverEnabled&&!(trackerReceives()||streamReceives()))||!receiverCurrent()||fetch||(received.size()==ReceiveQueueLimit&&!(trackerReceives()||streamReceives()))||status.size>MaximumI2cpPayloadBytes){
      remember(status.messageId);(void)enqueueControl(false,status.messageId);
      dropReceive((!receiverEnabled&&!(trackerReceives()||streamReceives()))?ReceiveDrop::Disabled:!receiverCurrent()?ReceiveDrop::LeaseExpired:status.size>MaximumI2cpPayloadBytes?ReceiveDrop::Malformed:ReceiveDrop::QueueFull);return;
    }
    fetch=Fetch{*session,status.messageId,status.size,generation,Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds),false};receiveDeadline();
    if(fetch&&!enqueueControl(true,status.messageId)){if(fetch){cancelFetch(ReceiveDrop::ControlBackpressure,false);}}
  }
  void inboundPayload(Frame frame){
    // Outer frame boundaries already validated. Bad application data drops,
    // while unsafe outer framing/transport keeps the existing channel fence.
    auto decoded=decodeMessagePayload(frame);
    if(!decoded){
      if(!fastReceive&&fetch&&frame.payload.size()>=6){const auto* p=frame.payload.data();const auto id=static_cast<std::uint16_t>((static_cast<unsigned>(p[0])<<8)|p[1]);std::uint32_t message=0;for(unsigned i=2;i<6;++i){message=(message<<8)|p[i];}if(session&&id==*session&&fetch->messageId==message){cancelFetch(ReceiveDrop::Malformed);return;}}
      dropReceive(ReceiveDrop::Malformed);return;
    }
    const auto id=decoded->messageId;
    if(!session||decoded->sessionId!=*session){dropReceive(ReceiveDrop::WrongSession);return;}
    std::optional<std::uint32_t> advertised;std::optional<Clock::time_point> fetchEnd;
    if(!fastReceive){
      if(!fetch||fetch->sessionId!=*session||fetch->messageId!=id||fetch->generation!=generation||!fetch->begun){
        if(fetch&&fetch->messageId==id){cancelFetch(ReceiveDrop::UnexpectedPayload);}else{(void)enqueueControl(false,id);dropReceive(remembered(id)?ReceiveDrop::Duplicate:ReceiveDrop::UnexpectedPayload);}return;
      }
      if(Clock::now()>=fetch->end){cancelFetch(ReceiveDrop::PendingTimeout);return;}
      advertised=fetch->size;fetchEnd=fetch->end;removeBegin(id);fetch.reset();receiveTimer.stop();remember(id);
      // ReceiveEnd is router storage release, before application validation
      // or consumption. It never acknowledges application delivery/signature.
      (void)enqueueControl(false,id);
    }else{if(remembered(id)){dropReceive(ReceiveDrop::Duplicate);return;}remember(id);}
    if(advertised&&*advertised!=decoded->payload.size()){dropReceive(ReceiveDrop::Malformed);return;}
    if(decoded->payload.size()>9&&decoded->payload[9]==streaming::Protocol&&streamReceives()){observeStreamPayload(decoded->payload,fetchEnd);return;}
    // RAW18 has no general receiver permission. Only this current dispatched
    // tracker operation may consume it, and it never enters the datagram queue.
    if(decoded->payload.size()>9&&decoded->payload[9]==RawTrackerProtocol&&trackerReceives()){observeTrackerPayload(decoded->payload,fetchEnd);return;}
    if(!receiverEnabled){dropReceive(ReceiveDrop::Disabled);return;}if(!receiverCurrent()){dropReceive(ReceiveDrop::LeaseExpired);return;}
    if(received.size()==ReceiveQueueLimit){dropReceive(ReceiveDrop::QueueFull);return;}
    const auto at=generation;auto mux=decodeMuxDatagram(decoded->payload);if(!mux){dropReceive(mux.error().code==::biglybtc::services::i2p::ErrorCode::Unsupported?ReceiveDrop::Unsupported:ReceiveDrop::Malformed);return;}
    ReceivedDatagram data;data.messageId=id;data.fromPort=mux->fromPort;data.toPort=mux->toPort;
    if(mux->protocol==Datagram2Protocol){auto body=decode2(mux->datagram,leaseSigner->destination()->hash);if(!body){dropReceive(ReceiveDrop::InvalidDatagram);return;}data.body=std::move(*body);}
    else if(mux->protocol==Datagram3Protocol){auto body=decode3(mux->datagram);if(!body){dropReceive(ReceiveDrop::InvalidDatagram);return;}data.body=std::move(*body);}
    else{dropReceive(ReceiveDrop::Unsupported);return;}
    if(!current(at)||!receiverEnabled||!receiverCurrent()){dropReceive(ReceiveDrop::LeaseExpired);return;}
    if(fetchEnd&&Clock::now()>=*fetchEnd){dropReceive(ReceiveDrop::PendingTimeout);return;}
    const auto weight=receiveWeight(data);if(weight>ReceiveByteLimit-receiveBytes||received.size()==ReceiveQueueLimit){dropReceive(ReceiveDrop::QueueFull);return;}
    received.push_back(std::move(data));receiveBytes+=weight;notifyReceive();
  }
  bool enableReceive(){if(!onThread()||!leaseSigner||!leaseSigner->valid()||!active()||state==State::Connecting||state==State::WaitingDate||state==State::DateReceived){return false;}receiverEnabled=true;notifyReceive();return receiverEnabled&&active();}
  void disableReceive(){if(!onThread()){return;}receiverEnabled=false;received.clear();receiveBytes=0;if(fetch&&!(trackerReceives()||streamReceives())){cancelFetch(ReceiveDrop::Cancelled);}notifyReceive();}
  std::optional<ReceivedDatagram> takeReceive(){if(!onThread()||received.empty()){return std::nullopt;}if(!receiverCurrent()){received.clear();receiveBytes=0;dropReceive(ReceiveDrop::LeaseExpired);return std::nullopt;}auto out=std::move(received.front());received.pop_front();receiveBytes-=receiveWeight(out);remember(out.messageId);notifyReceive();return out;}
