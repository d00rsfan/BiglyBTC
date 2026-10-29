// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. One explicit reliable outbound protocol6 stream.
  std::optional<streaming::Connection> stream;
  StreamOptions streamOptions;
  Clock::time_point streamEnd{};
  std::uint64_t streamGeneration=0,nextStreamOperation=1;
  std::array<std::uint32_t,1024> streamIds{};std::size_t streamIdCount=0;
  bool streamNotificationQueued=false,streamFlushing=false;
  static std::uint64_t streamNow(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
  void clearStream(){streamTimer.stop();stream.reset();streamNotificationQueued=false;streamFlushing=false;}
  void notifyStream(){
    if(streamNotificationQueued){return;}streamNotificationQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);const auto at=generation;queued=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* self=owner->impl_.get();if(self->generation!=at){return;}self->streamNotificationQueued=false;if(self->session&&self->stream&&self->active()&&self->socket.state()==QAbstractSocket::ConnectedState){emit owner->streamChanged();}},Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){streamNotificationQueued=false;fail(Failure::Resources);}
  }
  bool streamReceives()const{
    if(!stream||streamGeneration!=generation||state!=State::SessionCreated||!leaseCurrent()||Clock::now()>=streamEnd){return false;}
    const auto observation=stream->observation();return (stream->active()||observation.phase==streaming::Phase::Closed)&&observation.transmittedPackets>0;
  }
  bool streamCurrent(){
    if(!stream){return false;}if(!stream->active()&&stream->observation().phase!=streaming::Phase::Closed){return false;}
    if(streamGeneration!=generation||state!=State::SessionCreated||!leaseCurrent()){stream->terminate(streaming::Phase::LeaseExpired);streamTimer.stop();notifyStream();return false;}
    if(Clock::now()>=streamEnd){stream->terminate(streaming::Phase::Timeout);streamTimer.stop();notifyStream();return false;}return true;
  }
  void armStream(bool blocked=false)noexcept{try{
    if(!streamCurrent()){return;}if(!stream->active()){streamTimer.stop();return;}
    const auto observed=observedNow();if(!observed||*observed>=leaseSubmission->expiresMilliseconds){stream->terminate(streaming::Phase::LeaseExpired);streamTimer.stop();notifyStream();return;}
    auto wait=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(streamEnd-Clock::now()).count());
    wait=std::min(wait,static_cast<std::int64_t>(leaseSubmission->expiresMilliseconds-*observed));
    if(!blocked){if(const auto due=stream->wakeup()){const auto now=streamNow();wait=std::min(wait,static_cast<std::int64_t>(*due>now?*due-now:1));}}
    streamTimer.start(static_cast<int>(std::max<std::int64_t>(1,wait)));
  }catch(...){fail(Failure::Resources);}}
  void flushStream(){
    if(streamFlushing||!stream){return;}if(socket.bytesToWrite()!=0){armStream(true);return;}streamFlushing=true;
    try{
      if(!streamCurrent()){streamFlushing=false;return;}const auto at=generation,operation=stream->observation().operation;
      auto packet=stream->next(streamNow());if(!packet){streamFlushing=false;armStream();notifyStream();return;}
      auto encoded=streaming::encode(*packet,*leaseSigner);if(!encoded){streamFlushing=false;fail(Failure::Resources);return;}
      auto mux=streaming::encodeMux({streamOptions.fromPort,streamOptions.toPort,std::move(*encoded)});const auto now=observedNow();
      if(!mux||!now||*now>0xffffffffffffULL-45000){streamFlushing=false;fail(Failure::Resources);return;}
      // PacketQueue expires a timed packet25ms before its retry (minimum25).
      std::optional<std::uint64_t> expiry;if(const auto timeout=stream->transmitTimeout(*packet)){expiry=*now+std::max(25U,*timeout-25);}
      const bool final=packet->flags&(streaming::Close|streaming::Reset);
      auto frame=encodeSendDatagram({*session,stream->peer(),std::move(*mux),0,expiry,final});
      if(!frame){streamFlushing=false;fail(Failure::Resources);return;}
      if(!streamCurrent()||!current(at)||!stream||stream->observation().operation!=operation){streamFlushing=false;return;}
      const auto written=socket.write(reinterpret_cast<const char*>(frame->data()),static_cast<qint64>(frame->size()));
      if(!current(at)||!stream||stream->observation().operation!=operation){streamFlushing=false;return;}
      if(written!=static_cast<qint64>(frame->size())){streamFlushing=false;fail(Failure::WriteFailed);return;}
      stream->dispatched(*packet,streamNow());streamFlushing=false;armStream();notifyStream();
    }catch(...){streamFlushing=false;fail(Failure::Resources);}
  }
  void observeStreamPayload(View payload,std::optional<Clock::time_point> receiveEnd){
    if(!streamCurrent()||!streamReceives()){return;}auto mux=streaming::decodeMux(payload);
    if(!mux||mux->fromPort!=streamOptions.toPort||mux->toPort!=streamOptions.fromPort){stream->rejectPacket();notifyStream();return;}
    auto packet=streaming::decode(mux->packet,stream->peer());if(!packet){stream->rejectPacket();notifyStream();return;}
    if(!streamCurrent()||(receiveEnd&&Clock::now()>=*receiveEnd)){return;}
    stream->receive(std::move(*packet),streamNow());flushStream();armStream();notifyStream();
  }
  ConfigResult<std::uint64_t> startStream(View destination,const StreamOptions& input){
    using Code=::biglybtc::services::i2p::ErrorCode;auto reject=[](Code c,const char* why)->ConfigResult<std::uint64_t>{return std::unexpected(::biglybtc::services::i2p::Error{c,why});};
    if(!onThread()||(stream&&stream->active())||tracker||statusPending||state!=State::SessionCreated||waitingFirstLease||!leaseCurrent()||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0||fetch){return reject(Code::InvalidFormat,"Current owned LS2 and idle explicit channel required for stream");}
    if(!input.fromPort||!input.toPort||input.deadlineMilliseconds<1||input.deadlineMilliseconds>120000){return reject(Code::Unsupported,"Explicit Streaming ports/deadline required");}
    if(streamIdCount==streamIds.size()||nextStreamOperation==std::numeric_limits<std::uint64_t>::max()){return reject(Code::TooLarge,"Streaming lifetime correlation exhausted");}
    const auto end=Clock::now()+std::chrono::milliseconds(input.deadlineMilliseconds);
    try{
      auto peer=decodeDestination(destination);if(!peer){return std::unexpected(peer.error());}if(peer->hash==leaseSigner->destination()->hash){return reject(Code::InvalidFormat,"Streaming self-connect refused");}
      std::uint32_t id=0;bool random=false;for(unsigned attempt=0;attempt<8;++attempt){if(RAND_bytes(reinterpret_cast<unsigned char*>(&id),sizeof(id))!=1){break;}if(id&&std::find(streamIds.begin(),streamIds.begin()+static_cast<std::ptrdiff_t>(streamIdCount),id)==streamIds.begin()+static_cast<std::ptrdiff_t>(streamIdCount)){random=true;break;}}
      if(!random){return reject(Code::CryptoFailure,"Streaming ID entropy unavailable");}
      const auto at=generation,operation=nextStreamOperation;streaming::Connection prepared(operation,id,*leaseSigner->destination(),std::move(*peer));
      if(!current(at)||!leaseCurrent()||Clock::now()>=end){return reject(Code::InvalidFormat,"Streaming preparation lost current identity/budget");}
      stream.emplace(std::move(prepared));streamOptions=input;streamGeneration=at;streamEnd=end;streamIds[streamIdCount++]=id;++nextStreamOperation;
      flushStream();if(!current(at)||!stream||!stream->active()){return reject(Code::CryptoFailure,"Streaming initial dispatch failed");}return operation;
    }catch(...){fail(Failure::Resources);return reject(Code::CryptoFailure,"Streaming resources unavailable");}
  }
  ConfigResult<void> streamWrite(std::uint64_t operation,View bytes){
    if(!onThread()||!stream||stream->observation().operation!=operation||!streamCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::InvalidFormat,"Current Streaming operation required"});}
    try{auto written=stream->write(bytes);if(!written){return written;}flushStream();if(!stream||!stream->active()||!active()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Streaming write dispatch failed"});}return {};}catch(...){fail(Failure::Resources);return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Streaming resources unavailable"});}
  }
  ConfigResult<Bytes> streamRead(std::uint64_t operation,std::size_t maximum){
    if(!onThread()||!stream||stream->observation().operation!=operation||!streamCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::InvalidFormat,"Current Streaming operation required"});}
    try{auto bytes=stream->read(std::min(maximum,streaming::MaximumBuffered));flushStream();if(!streamCurrent()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::InvalidFormat,"Streaming authority expired during read"});}notifyStream();return bytes;}catch(...){fail(Failure::Resources);return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::CryptoFailure,"Streaming resources unavailable"});}
  }
