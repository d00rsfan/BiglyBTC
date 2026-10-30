// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. Tracker IDs are low (<=1024); these are high.
  struct OwnedLookup {std::uint64_t operation=0,generation=0;std::uint32_t request=0;Clock::time_point end{};Bytes write;bool sent=false;};
  std::optional<OwnedLookup> ownedLookup;
  std::optional<LookupObservation> ownedLookupResult;
  std::uint32_t nextOwnedLookup=0;
  bool lookupQueued=false;
  void clearLookup(){lookupTimer.stop();ownedLookup.reset();ownedLookupResult.reset();lookupQueued=false;}
  void notifyOwnedLookup(){
    if(lookupQueued){return;}lookupQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);const auto at=generation;queued=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* self=owner->impl_.get();if(self->generation!=at){return;}self->lookupQueued=false;if(self->ownedLookupResult){emit owner->destinationLookupChanged();}},Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){lookupQueued=false;fail(Failure::Resources);}
  }
  void finishOwnedLookup(LookupPhase phase,std::optional<Destination> destination={}){
    if(!ownedLookup||!ownedLookupResult){return;}ownedLookup.reset();lookupTimer.stop();ownedLookupResult->phase=phase;ownedLookupResult->destination=std::move(destination);notifyOwnedLookup();
  }
  bool ownedLookupCurrent(){
    if(!ownedLookup){return false;}
    if(ownedLookup->generation!=generation||state!=State::SessionCreated||!leaseCurrent()){finishOwnedLookup(LookupPhase::LeaseExpired);return false;}
    if(Clock::now()>=ownedLookup->end){finishOwnedLookup(LookupPhase::Timeout);return false;}return true;
  }
  void lookupDeadline(){
    if(!ownedLookupCurrent()){return;}const auto now=observedNow();if(!now||*now>=leaseSubmission->expiresMilliseconds){finishOwnedLookup(LookupPhase::LeaseExpired);return;}
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(ownedLookup->end-Clock::now()).count();
    lookupTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::min(left,static_cast<std::int64_t>(leaseSubmission->expiresMilliseconds-*now)))));
  }
  void flushOwnedLookup(){
    if(!ownedLookup||ownedLookup->write.empty()||socket.bytesToWrite()!=0||!ownedLookupCurrent()){return;}
    if(!effectReady()){return;}
    const auto size=static_cast<qint64>(ownedLookup->write.size());
    if(socket.write(reinterpret_cast<const char*>(ownedLookup->write.data()),size)!=size){fail(Failure::WriteFailed);return;}
    ownedLookup->write.clear();ownedLookup->sent=true;
  }
  ConfigResult<std::uint64_t> startOwnedLookup(const Hash& hash,unsigned timeout){
    using Code=::biglybtc::services::i2p::ErrorCode;
    const auto reject=[](Code code,const char* message)->ConfigResult<std::uint64_t>{return std::unexpected(::biglybtc::services::i2p::Error{code,message});};
    const auto end=Clock::now()+std::chrono::milliseconds(std::min(timeout,30000U));
    if(!onThread()||ownedLookup||tracker||state!=State::SessionCreated||!leaseCurrent()||waitingFirstLease||statusPending){return reject(Code::InvalidFormat,"Owned lookup requires current idle lookup/session");}
    if(!timeout||timeout>30000){return reject(Code::Unsupported,"Owned lookup deadline is1..30000ms");}
    if(nextOwnedLookup>=1024){return reject(Code::TooLarge,"Owned lookup lifetime correlations exhausted");}
    try{
      const auto request=0x80000000U+nextOwnedLookup;auto bytes=encodeHostLookup({*session,request,timeout,hash});if(!bytes){return std::unexpected(bytes.error());}
      if(!leaseCurrent()||Clock::now()>=end){return reject(Code::InvalidFormat,"Owned lookup preparation lost lease/deadline");}
      ++nextOwnedLookup;const auto operation=static_cast<std::uint64_t>(request);
      ownedLookupResult=LookupObservation{operation,hash,LookupPhase::Pending,{}};
      ownedLookup.emplace(OwnedLookup{operation,generation,request,end,std::move(*bytes),false});
      lookupDeadline();flushOwnedLookup();notifyOwnedLookup();
      if(!ownedLookup){return reject(Code::InvalidFormat,"Owned lookup failed before dispatch");}return operation;
    }catch(...){fail(Failure::Resources);return reject(Code::CryptoFailure,"Owned lookup resources unavailable");}
  }
  bool observeOwnedLookup(const Frame& frame){
    if(frame.payload.size()<6){return false;}
    std::uint32_t request=0;for(unsigned i=2;i<6;++i){request=(request<<8)|frame.payload[i];}
    if(request<0x80000000U){return false;} // Never route high old replies to tracker.
    if(!ownedLookup||request!=ownedLookup->request||!session||!ownedLookup->sent){return true;}
    const auto sid=static_cast<std::uint16_t>((unsigned(frame.payload[0])<<8)|frame.payload[1]);
    if(sid!=*session||!ownedLookupCurrent()){return true;}
    auto reply=decodeHostReply(frame);
    if(!reply||reply->result||!reply->destination||reply->destination->hash!=ownedLookupResult->requested){finishOwnedLookup(LookupPhase::Failed);return true;}
    if(!ownedLookupCurrent()){return true;}finishOwnedLookup(LookupPhase::Resolved,std::move(reply->destination));return true;
  }
