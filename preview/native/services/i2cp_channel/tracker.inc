// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl; actual owned I2CP stream, no backend selection.
  struct TrackerRound {
    TrackerRequest request;std::uint64_t operation=0,generation=0;
    std::uint32_t lookup=0;std::array<std::uint32_t,2> transactions{};
    Clock::time_point end{};std::optional<Destination> destination;
    Bytes write;bool connecting=true,stageWritten=false;
  };
  static_assert(std::is_nothrow_move_constructible_v<TrackerRound>);
  std::optional<TrackerRound> tracker;
  std::optional<TrackerObservation> trackerResult;
  std::uint64_t nextTrackerOperation=1,nextTrackerLookup=1;
  bool trackerNotificationQueued=false;
  std::array<std::uint32_t,2*MaximumTrackerOperations> trackerTransactions{};std::size_t trackerTransactionCount=0;
  void clearTracker(){trackerTimer.stop();tracker.reset();trackerResult.reset();trackerConnection=0;trackerNotificationQueued=false;}
  void notifyTracker(){
    if(trackerNotificationQueued){return;}trackerNotificationQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);const auto at=generation;queued=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* self=owner->impl_.get();if(self->generation!=at){return;}self->trackerNotificationQueued=false;if(self->session&&self->active()&&self->trackerResult&&self->socket.state()==QAbstractSocket::ConnectedState){emit owner->trackerChanged();}},Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){trackerNotificationQueued=false;fail(Failure::Resources);}
  }
  void finishTracker(TrackerPhase phase,std::optional<TrackerCounts> counts=std::nullopt,Byte lookupResult=0){
    if(!tracker||!trackerResult){return;}if(counts&&!trackerCurrent()){return;}tracker.reset();trackerTimer.stop();trackerResult->phase=phase;trackerResult->counts=counts;trackerResult->lookupResult=lookupResult;
    // This is only local cancellation/release. No fabricated remote ACK/status.
    if(!receiverEnabled&&fetch){cancelFetch(ReceiveDrop::Cancelled);}notifyTracker();
  }
  bool trackerCurrent(){
    if(!tracker){return false;}
    if(tracker->generation!=generation||!leaseCurrent()||state!=State::SessionCreated){finishTracker(TrackerPhase::LocalLeaseExpired);return false;}
    if(Clock::now()>=tracker->end){finishTracker(TrackerPhase::LocalTimeout);return false;}return true;
  }
  bool trackerReceives()const{return tracker&&tracker->destination&&tracker->stageWritten&&tracker->generation==generation&&state==State::SessionCreated&&leaseCurrent()&&Clock::now()<tracker->end;}
  void rejectTrackerReply(){if(trackerResult&&trackerResult->rejectedReplies!=std::numeric_limits<std::uint64_t>::max()){++trackerResult->rejectedReplies;}notifyTracker();}
  void trackerDeadline(){
    if(!trackerCurrent()){return;}const auto now=observedNow();if(!now||*now>=leaseSubmission->expiresMilliseconds){finishTracker(TrackerPhase::LocalLeaseExpired);return;}
    const auto operationLeft=std::chrono::duration_cast<std::chrono::milliseconds>(tracker->end-Clock::now()).count();
    const auto leaseLeft=leaseSubmission->expiresMilliseconds-*now;
    trackerTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::min(operationLeft,static_cast<std::int64_t>(leaseLeft)))));
  }
  void flushTrackerWrite(){
    if(!tracker||tracker->write.empty()||socket.bytesToWrite()!=0){return;}
    try{
      if(!trackerCurrent()){return;}const auto at=generation;const auto operation=tracker->operation;
      const auto written=socket.write(reinterpret_cast<const char*>(tracker->write.data()),static_cast<qint64>(tracker->write.size()));
      if(!current(at)||!tracker||tracker->operation!=operation){return;}
      if(written!=static_cast<qint64>(tracker->write.size())){fail(Failure::WriteFailed);return;}
      tracker->write.clear();tracker->stageWritten=true;
    }catch(...){fail(Failure::Resources);}
  }
  bool prepareTrackerPacket(){
    if(!trackerCurrent()||!tracker->destination){return false;}
    namespace wire=::biglybtc::core::udp_tracker_wire;
    Bytes payload;
    if(tracker->connecting){const auto p=wire::connect(tracker->transactions[0]);payload.assign(p.begin(),p.end());}
    else{std::array<std::byte,20> hash{};for(std::size_t i=0;i<hash.size();++i){hash[i]=static_cast<std::byte>(tracker->request.infoHash[i]);}const auto p=wire::scrape(trackerConnection,tracker->transactions[1],hash);payload.assign(p.begin(),p.end());}
    auto encoded=tracker->connecting?leaseSigner->makeDatagram2(tracker->destination->hash,payload):encode3(leaseSigner->destination()->wire,payload);
    if(!encoded){fail(Failure::Resources);return false;}
    auto mux=encodeMuxDatagram({tracker->connecting?Datagram2Protocol:Datagram3Protocol,TrackerLocalPort,tracker->request.port,std::move(*encoded)},GzipMode::Deflate);
    const auto now=observedNow();if(!mux||!now||*now>0xffffffffffffULL-60000){fail(Failure::Resources);return false;}
    // Original UDP tracker uses untracked I2CP sends; the actual BEP15 reply is
    // separate from router acceptance, also with initial reliability=none.
    auto frame=encodeSendDatagram({*session,*tracker->destination,std::move(*mux),0,*now+60000,!tracker->connecting});
    if(!frame){fail(Failure::Resources);return false;}if(!trackerCurrent()){return false;}
    tracker->write=std::move(*frame);tracker->stageWritten=false;return true;
  }
  std::uint64_t trackerConnection=0;
  ConfigResult<std::uint64_t> startTracker(const TrackerRequest& input){
    using Code=::biglybtc::services::i2p::ErrorCode;auto reject=[](Code c,const char* why)->ConfigResult<std::uint64_t>{return std::unexpected(::biglybtc::services::i2p::Error{c,why});};
    if(!onThread()||tracker||statusPending||state!=State::SessionCreated||waitingFirstLease||!leaseCurrent()||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0){return reject(Code::InvalidFormat,"Tracker needs current owned LS2 and idle explicit channel");}
    if(!input.port||input.deadlineMilliseconds<1||input.deadlineMilliseconds>60000){return reject(Code::Unsupported,"Explicit tracker port and bounded deadline required");}
    if(trackerTransactionCount==trackerTransactions.size()||nextTrackerLookup>std::numeric_limits<std::uint32_t>::max()||nextTrackerOperation==std::numeric_limits<std::uint64_t>::max()){return reject(Code::TooLarge,"Tracker lifetime correlation exhausted");}
    try{
      const auto at=generation;TrackerRound prepared;prepared.request=input;prepared.operation=nextTrackerOperation;prepared.generation=at;prepared.lookup=static_cast<std::uint32_t>(nextTrackerLookup);prepared.end=Clock::now()+std::chrono::milliseconds(input.deadlineMilliseconds);
      bool random=false;for(unsigned i=0;i<8;++i){if(RAND_bytes(reinterpret_cast<unsigned char*>(prepared.transactions.data()),sizeof(prepared.transactions))!=1){break;}if(prepared.transactions[0]!=prepared.transactions[1]&&std::none_of(trackerTransactions.begin(),trackerTransactions.begin()+static_cast<std::ptrdiff_t>(trackerTransactionCount),[&](auto prior){return prior==prepared.transactions[0]||prior==prepared.transactions[1];})){random=true;break;}}
      if(!random){return reject(Code::CryptoFailure,"Tracker transaction entropy unavailable");}
      auto bytes=encodeHostLookup({*session,prepared.lookup,static_cast<std::uint32_t>(std::min(input.deadlineMilliseconds,30000)),input.destination});if(!bytes){return std::unexpected(bytes.error());}
      if(!current(at)||!leaseCurrent()||Clock::now()>=prepared.end){return reject(Code::InvalidFormat,"Tracker preparation lost lease/budget");}
      prepared.write=std::move(*bytes);const auto operation=prepared.operation;++nextTrackerLookup;++nextTrackerOperation;
      trackerTransactions[trackerTransactionCount++]=prepared.transactions[0];trackerTransactions[trackerTransactionCount++]=prepared.transactions[1];
      trackerResult=TrackerObservation{operation,input,TrackerPhase::LookingUp,std::nullopt,0,0};tracker.emplace(std::move(prepared));trackerConnection=0;
      trackerDeadline();flushTrackerWrite();if(!tracker||!current(at)){return reject(Code::InvalidFormat,"Tracker request failed before dispatch");}notifyTracker();if(!current(at)||!tracker||tracker->operation!=operation){return reject(Code::CryptoFailure,"Tracker notification failed");}return operation;
    }catch(...){if(tracker){fail(Failure::Resources);}return reject(Code::CryptoFailure,"Tracker resources unavailable");}
  }
  void observeHostReply(const Frame& frame){
    if(!tracker||trackerResult->phase!=TrackerPhase::LookingUp){return;}
    // Correlate the fixed prefix before decoding: malformed old/wrong replies
    // cannot finish a newer operation merely by failing a parser.
    if(frame.payload.size()<6){rejectTrackerReply();return;}
    const auto sid=static_cast<std::uint16_t>((static_cast<unsigned>(frame.payload[0])<<8)|frame.payload[1]);std::uint32_t request=0;for(unsigned i=2;i<6;++i){request=(request<<8)|frame.payload[i];}
    if(!session||sid!=*session||request!=tracker->lookup||!tracker->stageWritten){rejectTrackerReply();return;}
    auto reply=decodeHostReply(frame);if(!reply){if(trackerCurrent()){finishTracker(TrackerPhase::UnknownReply);}return;}
    if(!trackerCurrent()){return;}
    if(reply->result){finishTracker(TrackerPhase::LookupFailed,std::nullopt,reply->result);return;}
    if(!reply->destination||reply->destination->hash!=tracker->request.destination){finishTracker(TrackerPhase::DestinationMismatch);return;}
    if(reply->destination->hash==leaseSigner->destination()->hash){finishTracker(TrackerPhase::DestinationMismatch);return;}
    tracker->destination=std::move(*reply->destination);trackerResult->phase=TrackerPhase::Connecting;
    if(prepareTrackerPacket()){flushTrackerWrite();notifyTracker();}
  }
  void observeTrackerPayload(View payload,std::optional<Clock::time_point> receiveEnd){
    if(!trackerCurrent()||!trackerReceives()){return;}
    auto mux=decodeRawTrackerReply(payload);
    if(!mux||mux->toPort!=TrackerLocalPort||mux->fromPort!=tracker->request.port){rejectTrackerReply();return;}
    namespace wire=::biglybtc::core::udp_tracker_wire;
    const auto reply=wire::reply(std::string_view(reinterpret_cast<const char*>(mux->datagram.data()),mux->datagram.size()),tracker->transactions[tracker->connecting?0:1],tracker->connecting);
    if(!reply){rejectTrackerReply();return;}if(!trackerCurrent()){return;}
    if(receiveEnd&&Clock::now()>=*receiveEnd){dropReceive(ReceiveDrop::PendingTimeout);return;}
    if(reply->kind==wire::ReplyKind::Unsupported){finishTracker(TrackerPhase::UnknownReply);return;}
    if(reply->kind==wire::ReplyKind::Error){finishTracker(TrackerPhase::TrackerError);return;}
    if(reply->kind==wire::ReplyKind::Connect){trackerConnection=reply->connection;tracker->connecting=false;trackerResult->phase=TrackerPhase::Scraping;if(prepareTrackerPacket()){flushTrackerWrite();notifyTracker();}}
    else{finishTracker(TrackerPhase::Counts,TrackerCounts{reply->seeds,reply->downloaded,reply->peers});}
  }
