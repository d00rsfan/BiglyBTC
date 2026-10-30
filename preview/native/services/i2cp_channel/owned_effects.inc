// SPDX-License-Identifier: GPL-2.0-or-later
// Inside InitialChannel::Impl. Absent gate preserves the original path.
  std::shared_ptr<EffectAuthority> sourceAuthority;
  struct SourceLease {Frame frame;Clock::time_point end;std::uint64_t generation=0;};
  std::optional<SourceLease> sourceLease;
  bool sourceEffectPending=false,sourceEffectQueued=false;
  Clock::time_point sourceEffectEnd{};
  void clearSourceEffects(){sourceLease.reset();sourceEffectPending=false;sourceEffectQueued=false;effectTimer.stop();}
  bool effectReady(){
    if(!sourceAuthority){return true;}
    if(sourceAuthority->revoked()){fail(Failure::SourceChanged);return false;}
    if(sourceAuthority->granted()){return true;}
    if(!sourceEffectPending){sourceEffectPending=true;sourceEffectEnd=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);effectTimer.start(options.deadlineMilliseconds);}
    if(Clock::now()>=sourceEffectEnd){fail(Failure::Deadline);return false;}
    if(!sourceEffectQueued){
      sourceEffectQueued=true;bool ok=false;
      try{const QPointer<InitialChannel> owner(q);const auto at=generation;ok=QMetaObject::invokeMethod(q,[owner,at]{if(!owner){return;}auto* self=owner->impl_.get();if(self->generation!=at){return;}self->sourceEffectQueued=false;if(self->active()&&self->sourceEffectPending){emit owner->sourceEffectRequested();}},Qt::QueuedConnection);}catch(...){ok=false;}
      if(!ok){sourceEffectQueued=false;fail(Failure::Resources);}
    }
    return false;
  }
  bool resumeSourceEffects(){
    if(!onThread()||!sourceAuthority||!sourceAuthority->granted()||!active()){return false;}
    if(sourceEffectPending&&Clock::now()>=sourceEffectEnd){fail(Failure::Deadline);return false;}
    if(!sourceEffectPending&&!sourceLease){return true;}
    sourceEffectPending=false;effectTimer.stop();
    try{
      if(state==State::Connecting){
        if(socket.state()==QAbstractSocket::UnconnectedState){socket.connectToHost(options.address,options.port);}
        else if(socket.state()==QAbstractSocket::ConnectedState){connected();}
      }
      if(sourceLease&&socket.bytesToWrite()==0){
        auto queued=std::move(*sourceLease);sourceLease.reset();
        if(queued.generation!=generation||Clock::now()>=queued.end){fail(Failure::Deadline);return false;}
        answerLeaseWithin(std::move(queued.frame),queued.end);
      }
      if(active()){flushReceiveControls();flushTrackerWrite();flushOwnedLookup();flushStream();}
      if(sourceLease){sourceEffectPending=true;sourceEffectEnd=sourceLease->end;effectTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(sourceEffectEnd-Clock::now()).count())));}
      return active()&&sourceAuthority->current();
    }catch(...){fail(Failure::Resources);return false;}
  }
