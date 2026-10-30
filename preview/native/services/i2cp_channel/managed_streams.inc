// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. Explicit manager only; legacy defaults unchanged.
  std::optional<streaming::Manager> managedStreams;
  std::uint64_t managedGeneration=0,managedLastOperation=0;
  struct DeferredLease {Frame frame;Clock::time_point end;std::uint64_t generation=0;};
  std::optional<DeferredLease> managedLease;
  bool managedCurrent(){
    if(!managedStreams){return false;}
    if(sourceAuthority&&sourceAuthority->revoked()){fail(Failure::SourceChanged);return false;}
    if(generation!=managedGeneration||state!=State::SessionCreated||!leaseCurrent()){
      managedStreams->terminate(streaming::Phase::LeaseExpired);managedLease.reset();streamTimer.stop();notifyStream();return false;
    }
    return true;
  }
  bool managedReceives()const{
    return managedStreams&&generation==managedGeneration&&state==State::SessionCreated&&leaseCurrent()&&managedStreams->receives();
  }
  void clearManagedStreams(){managedLease.reset();managedStreams.reset();managedGeneration=0;managedLastOperation=0;}
  bool streamsOutstanding()const{return (stream&&stream->active())||(managedStreams&&managedStreams->receives());}
  void armManagedStreams(bool blocked=false)noexcept{try{
    if(!managedCurrent()){return;}
    const auto now=observedNow();if(!now){fail(Failure::InvalidDate);return;}
    auto wait=static_cast<std::int64_t>(leaseSubmission->expiresMilliseconds-*now);
    if(managedLease){wait=std::min(wait,std::chrono::duration_cast<std::chrono::milliseconds>(managedLease->end-Clock::now()).count());}
    if(auto due=blocked?managedStreams->hardWakeup():managedStreams->wakeup()){const auto stamp=streamNow();wait=std::min(wait,static_cast<std::int64_t>(*due>stamp?*due-stamp:1));}
    streamTimer.start(static_cast<int>(std::clamp<std::int64_t>(wait,1,std::numeric_limits<int>::max())));
  }catch(...){fail(Failure::Resources);}}
  bool reserveManagedIdentity(std::uint64_t& operation,std::uint32_t& id){
    if(streamIdCount==streamIds.size()||nextStreamOperation==std::numeric_limits<std::uint64_t>::max()){return false;}
    bool selected=false;
    for(unsigned attempt=0;attempt<8;++attempt){
      if(RAND_bytes(reinterpret_cast<unsigned char*>(&id),sizeof(id))!=1){return false;}
      if(id&&std::find(streamIds.begin(),streamIds.begin()+static_cast<std::ptrdiff_t>(streamIdCount),id)==streamIds.begin()+static_cast<std::ptrdiff_t>(streamIdCount)){selected=true;break;}
    }
    if(!selected){return false;}operation=nextStreamOperation;return true;
  }
  void commitManagedIdentity(std::uint64_t operation,std::uint32_t id)noexcept{
    streamIds[streamIdCount++]=id;++nextStreamOperation;managedLastOperation=operation;
  }
  bool flushManagedLease(){
    if(!managedLease){return true;}
    if(!effectReady()){return false;}
    if(!managedCurrent()){return false;}
    if(managedLease->generation!=generation||Clock::now()>=managedLease->end){fail(Failure::Deadline);return false;}
    if(socket.bytesToWrite()!=0){return false;}
    auto queued=std::move(*managedLease);managedLease.reset();answerLeaseWithin(std::move(queued.frame),queued.end);
    return active()&&socket.bytesToWrite()==0;
  }
  void flushManagedStreams(){
    if(streamFlushing||!managedStreams){return;}
    try{
      if(!managedCurrent()){return;}
      // Hard deadlines settle even while the one already-queued I2CP frame
      // waits for bytesWritten. Retry/output readiness alone stays suppressed.
      managedStreams->tick(streamNow());notifyStream();if(!active()||!managedStreams){return;}
      if(!effectReady()){if(active()){armManagedStreams(true);}return;}
      if(managedLease&&!flushManagedLease()){if(active()){armManagedStreams(true);}return;}
      if(socket.bytesToWrite()!=0){armManagedStreams(true);return;}
      streamFlushing=true;const auto at=generation;
      for(std::size_t attempts=0;attempts<managedStreams->maximumStreams();++attempts){
      auto outbound=managedStreams->next(streamNow());
      if(!outbound){streamFlushing=false;armManagedStreams();notifyStream();return;}
      const auto* peer=managedStreams->peer(outbound->operation);if(!peer){streamFlushing=false;fail(Failure::Resources);return;}
      auto encoded=streaming::encode(outbound->packet,*leaseSigner);
      if(!encoded){streamFlushing=false;fail(Failure::Resources);return;}
      auto mux=streaming::encodeMux({outbound->ports.local,outbound->ports.remote,std::move(*encoded)});const auto now=observedNow();
      if(!mux||!now||*now>0xffffffffffffULL-45000){streamFlushing=false;fail(Failure::Resources);return;}
      std::optional<std::uint64_t> expiry;if(auto timeout=managedStreams->transmitTimeout(*outbound)){expiry=*now+std::max(25U,*timeout-25);}
      const bool final=outbound->packet.flags&(streaming::Close|streaming::Reset);
      auto frame=encodeSendDatagram({*session,*peer,std::move(*mux),0,expiry,final});
      if(!frame){streamFlushing=false;fail(Failure::Resources);return;}
      if(!current(at)||!managedCurrent()){streamFlushing=false;return;}
      // Recheck absolute handshake/wait/idle gates after signing/compression.
      managedStreams->tick(streamNow());const auto currentStream=managedStreams->observation(outbound->operation);
      if(!currentStream||(!managedStreams->peer(outbound->operation))||(currentStream->stream.phase!=streaming::Phase::Connecting&&currentStream->stream.phase!=streaming::Phase::Established&&currentStream->stream.phase!=streaming::Phase::ReadEof&&currentStream->stream.phase!=streaming::Phase::Closed)){continue;}
      if(!effectReady()){streamFlushing=false;return;}
      const auto written=socket.write(reinterpret_cast<const char*>(frame->data()),static_cast<qint64>(frame->size()));
      if(!current(at)||!managedStreams){streamFlushing=false;return;}
      if(written!=static_cast<qint64>(frame->size())){streamFlushing=false;fail(Failure::WriteFailed);return;}
      managedStreams->dispatched(*outbound,streamNow());streamFlushing=false;armManagedStreams();notifyStream();return;
      }
      streamFlushing=false;armManagedStreams();notifyStream();
    }catch(...){streamFlushing=false;fail(Failure::Resources);}
  }
  void observeManagedStream(View payload,std::optional<Clock::time_point> receiveEnd){
    if(!managedCurrent()){return;}auto mux=streaming::decodeMux(payload);if(!mux){dropReceive(ReceiveDrop::Malformed);return;}
    if(!managedCurrent()||(receiveEnd&&Clock::now()>=*receiveEnd)){return;}
    const auto at=generation;managedStreams->receive(*mux,streamNow(),streamNow,[this,at,receiveEnd]{return current(at)&&state==State::SessionCreated&&leaseCurrent()&&(!receiveEnd||Clock::now()<*receiveEnd);});flushManagedStreams();armManagedStreams();notifyStream();
  }
  bool enableManagedStreams(const streaming::ManagerOptions& policy){
    if(!onThread()||managedStreams||(stream&&!stream->drained())||streamsOutstanding()||tracker||statusPending||state!=State::SessionCreated||waitingFirstLease||!leaseCurrent()||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0||fetch){return false;}
    try{
      streaming::Manager prepared(*leaseSigner->destination(),policy,[](unsigned bound){
        if(!bound){throw std::runtime_error("Invalid Streaming entropy bound");}
        const auto limit=std::numeric_limits<std::uint32_t>::max()-(std::numeric_limits<std::uint32_t>::max()%bound);
        for(unsigned attempt=0;attempt<8;++attempt){std::uint32_t value=0;if(RAND_bytes(reinterpret_cast<unsigned char*>(&value),sizeof(value))!=1){throw std::runtime_error("Streaming entropy unavailable");}if(value<limit){return value%bound;}}
        throw std::runtime_error("Streaming entropy rejection exhausted");
      });
      if(!prepared.valid()||!leaseCurrent()){return false;}
      managedStreams.emplace(std::move(prepared));managedGeneration=generation;armManagedStreams();notifyStream();return active()&&managedStreams.has_value();
    }catch(...){fail(Failure::Resources);return false;}
  }
  ConfigResult<std::uint64_t> startManagedStream(View wire,const StreamOptions& input,bool accepting=false){
    const auto entered=streamNow();
    using Code=::biglybtc::services::i2p::ErrorCode;
    const auto refuse=[](Code code,const char* reason)->ConfigResult<std::uint64_t>{return std::unexpected(::biglybtc::services::i2p::Error{code,reason});};
    if(!onThread()||!managedCurrent()||input.deadlineMilliseconds<1||input.deadlineMilliseconds>120000){return refuse(Code::Unsupported,"Current manager and finite handshake required");}
    const auto period=static_cast<std::uint64_t>(input.deadlineMilliseconds);
    if(entered>std::numeric_limits<std::uint64_t>::max()-period){return refuse(Code::Unsupported,"Handshake clock overflow");}
    const auto end=entered+period;
    std::uint64_t operation=0;std::uint32_t id=0;if(!reserveManagedIdentity(operation,id)){return refuse(Code::TooLarge,"Streaming lifetime identity budget exhausted");}
    try{
      ConfigResult<void> accepted;
      if(accepting){accepted=managedStreams->accept(operation,id,streamNow());}
      else{
        auto peer=decodeDestination(wire);if(!peer){return std::unexpected(peer.error());}
        if(!managedCurrent()){return refuse(Code::Unsupported,"Manager identity/lease changed");}
        accepted=managedStreams->connect(operation,id,std::move(*peer),{{input.fromPort,input.toPort},static_cast<unsigned>(input.deadlineMilliseconds),input.maximumWindow},streamNow(),end);
      }
      if(!accepted){return std::unexpected(accepted.error());}
      commitManagedIdentity(operation,id);flushManagedStreams();armManagedStreams();notifyStream();
      if(!active()||!managedStreams){return refuse(Code::CryptoFailure,"Managed stream initial dispatch failed");}return operation;
    }catch(...){fail(Failure::Resources);return refuse(Code::CryptoFailure,"Managed stream resources unavailable");}
  }
  bool listenManagedStreams(const streaming::ListenOptions& provided){
    if(!onThread()||!managedCurrent()||provided.peers.empty()||provided.peers.size()>managedStreams->maximumStreams()){return false;}
    try{
      streaming::ListenOptions input;input.ports=provided.ports;input.maximumWindow=provided.maximumWindow;
      for(const auto& peer:provided.peers){auto decoded=decodeDestination(peer.wire);if(!decoded){return false;}input.peers.push_back(std::move(*decoded));}
      if(!managedCurrent()){return false;}auto result=managedStreams->listen(std::move(input),streamNow());if(!result){return false;}armManagedStreams();notifyStream();return active();
    }catch(...){fail(Failure::Resources);return false;}
  }
  ConfigResult<void> writeManagedStream(std::uint64_t op,View bytes){
    if(!onThread()||!managedCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::Unsupported,"Current managed stream required"});}
    try{auto result=managedStreams->write(op,bytes,streamNow());if(!result){return result;}flushManagedStreams();armManagedStreams();notifyStream();const auto observed=managedStreams?managedStreams->observation(op):std::nullopt;if(!active()||!observed||(observed->stream.phase!=streaming::Phase::Connecting&&observed->stream.phase!=streaming::Phase::Established&&observed->stream.phase!=streaming::Phase::ReadEof)){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Managed dispatch failed"});}return {};}catch(...){fail(Failure::Resources);return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Managed resources unavailable"});}
  }
  ConfigResult<Bytes> readManagedStream(std::uint64_t op,std::size_t maximum){
    if(!onThread()||!managedCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::Unsupported,"Current managed stream required"});}
    try{auto result=managedStreams->read(op,maximum,streamNow());flushManagedStreams();if(!managedCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::Unsupported,"Managed authority expired during read"});}managedStreams->tick(streamNow());const auto observed=managedStreams->observation(op);if(!observed||(observed->stream.phase!=streaming::Phase::Connecting&&observed->stream.phase!=streaming::Phase::Established&&observed->stream.phase!=streaming::Phase::ReadEof&&observed->stream.phase!=streaming::Phase::Closed)){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::Unsupported,"Managed operation expired during read"});}armManagedStreams();notifyStream();return result;}catch(...){fail(Failure::Resources);return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Managed resources unavailable"});}
  }
