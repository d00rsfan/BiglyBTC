// Included inside the existing worker-owned SessionBridge::Host. Never call
// provider methods from TagConstraintWorker's SAME-OFD source section.
  std::unique_ptr<TagConstraintWorker> constraintWorker;
  std::unique_ptr<InternalTagNamespace> internalTagNamespace;
  void activateInternalTagNamespace()noexcept{
    if(internalTagNamespace||!normalNetwork||!tagManagerAtOpen||!tagManagerAtOpen->enabled()){return;}
    try{auto value=InternalTagNamespace::activate(lease,columnLayoutSession,true,*tagManagerAtOpen);if(value){internalTagNamespace=std::move(*value);}}catch(...){/* Missing source stays unavailable; it is never an empty census. */}
  }

  quint64 constraintSequence=0,constraintIntakeSequence=0;
  std::deque<ConstraintPeerObservation> constraintPeerObservations;
  bool constraintPaused=false,constraintNormalReady=false;
  std::chrono::steady_clock::time_point constraintReloadAt{};
  void noteConstraintIntake(const core::TorrentHash& hash)noexcept{
    // Called ONLY from an actual successful fresh durable intake result. This
    // is not a UI/row/startup hook, and must not fail an already-saved import.
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||replacementReviewOnly||fileRecoveryRequired||manualTagEnded||constraintPaused||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return;}
    try{
      if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
      if(manualTagStore&&manualTagStore->recoveryRequired()){
        throw Failure{{storage::ErrorCode::Conflict,"New-download tags require explicit store reload"}};
      }
      auto holder=std::make_shared<storage::ManualTagSnapshot>();
      if(!manualTagStore){manualTagStore=take(storage::ManualTagStore::open(lease));*holder=take(manualTagStore->snapshot());}
      else{*holder=take(manualTagStore->reload());}
      if(!manualTagSnapshot.sameSource(*holder)||manualTagSnapshot.writable()!=holder->writable()){
        if(manualTagRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"Tag presentations exhausted"}};}
        manualTagsEnabled=true;++manualTagRevision;acceptManualTagSnapshot(*holder,holder);
      }else{manualTagSnapshot=*holder;}
      constraintReloadAt=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      if(manualTagSnapshot.missing()||!manualTagSnapshot.writable()){return;}
      if(constraintIntakeSequence==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"New-download event sequence exhausted"}};}
      (void)take(constraintWorker->recordIntake(*manualTagStore,manualTagSnapshot,hash,++constraintIntakeSequence));
    }catch(const Failure& failure){
      constraintPaused=true;manualTagFailure=SessionBridge::tr("New-download tag processing paused (storage %1); reload tags before retrying")
          .arg(static_cast<int>(failure.error.code));
    }catch(...){constraintPaused=true;manualTagFailure=SessionBridge::tr("New-download tag processing paused; reload tags before retrying");}
  }
  void noteChangedTrackerProperty(std::int32_t id)noexcept{
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||replacementReviewOnly||fileRecoveryRequired
        ||manualTagEnded||constraintPaused||!manualTagStore||!lastSnapshot){return;}
    try{
      if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
      (void)take(constraintWorker->recordTrackerPropertyEvent(*manualTagStore,manualTagSnapshot,id,lastSnapshot,TagConstraintWorker::TrackerPropertyEvent::Changed));
    }catch(...){constraintPaused=true;manualTagFailure=SessionBridge::tr("Automatic tracker tag processing paused; reload tags before retrying");}
  }
  bool synchroniseTrackerProperty(std::int32_t id,std::stop_token stop){
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||replacementReviewOnly||fileRecoveryRequired
        ||manualTagEnded||constraintPaused||!manualTagStore||!lastSnapshot||lastSnapshot->revision!=revision){
      throw Failure{{storage::ErrorCode::Unsupported,"Tracker tag synchronisation requires an active normal session"}};}
    if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
    return take(constraintWorker->recordTrackerPropertyEvent(*manualTagStore,manualTagSnapshot,id,lastSnapshot,TagConstraintWorker::TrackerPropertyEvent::Sync,stop));
  }
  void noteChangedUntaggedProperty(const storage::ManualTagSnapshot& before)noexcept{
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||replacementReviewOnly||fileRecoveryRequired
        ||manualTagEnded||constraintPaused||!manualTagStore||!lastSnapshot){return;}
    try{if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
      (void)take(constraintWorker->recordUntaggedPropertyChange(*manualTagStore,before,manualTagSnapshot,lastSnapshot));
    }catch(...){constraintPaused=true;manualTagFailure=SessionBridge::tr("Automatic Untagged processing paused; reload tags before retrying");}
  }
  bool tickConstraintTags(std::stop_token stop){
    // Category-derived membership may be partial; do not persist rules from
    // it while ANY category journal candidate remains unresolved.
    if((storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return false;}
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||replacementReviewOnly||fileRecoveryRequired||manualTagEnded
        ||constraintPaused||!lastSnapshot||lastSnapshot->revision!=revision||stop.stop_requested()){
      return false;
    }
    try{
      if(constraintSequence==std::numeric_limits<quint64>::max()){
        throw Failure{{storage::ErrorCode::Unsupported,"Automatic-tag worker turn is exhausted"}};
      }
      if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
      // This holder is allocated before any synchronous durable publication.
      auto holder=std::make_shared<storage::ManualTagSnapshot>();
      bool refreshed=false,refreshSettings=false;
      const auto now=std::chrono::steady_clock::now();
      if(!manualTagStore||!manualTagSnapshot.valid()||now>=constraintReloadAt){
        refreshSettings=true;
        // Automatic reads may not reopen/repair an uncertain instance. Only
        // the existing explicit Reload command can recover that authority.
        if(manualTagStore&&manualTagStore->recoveryRequired()){
          throw Failure{{storage::ErrorCode::Conflict,"Automatic-tag store requires explicit reload"}};
        }
        if(!manualTagStore){manualTagStore=take(storage::ManualTagStore::open(lease,stop));*holder=take(manualTagStore->snapshot());}
        else{*holder=take(manualTagStore->reload(stop));}
        const bool different=!manualTagSnapshot.sameSource(*holder)
            ||manualTagSnapshot.writable()!=holder->writable();
        if(different){
          if(manualTagRevision==std::numeric_limits<quint64>::max()){
            throw Failure{{storage::ErrorCode::Unsupported,"Manual-tag presentation revisions are exhausted"}};
          }
          manualTagsEnabled=true;++manualTagRevision;acceptManualTagSnapshot(*holder,holder);refreshed=true;
        }else{manualTagSnapshot=*holder;} // Equal bytes: keep public generation/membership.
        constraintReloadAt=now+std::chrono::seconds(30);
      }
      // Read-only/recovery/missing tags do not authorize automatic creation or
      // edits. No provider identity is invented for an inactive saved row.
      if(!manualTagSnapshot.writable()||!manualTagSnapshot.tags()||manualTagSnapshot.missing()){
        return refreshed;
      }
      if(manualTagRevision==std::numeric_limits<quint64>::max()){
        throw Failure{{storage::ErrorCode::Unsupported,"Manual-tag presentation revisions are exhausted"}};
      }
      ConstraintWorkerFrame frame;frame.session=columnLayoutSession;frame.sequence=++constraintSequence;
      frame.refreshSettings=refreshSettings;
      frame.library=lastSnapshot;frame.sourceRevision=lease.sourceRevision();
      if(internalTagNamespace){auto source=internalTagNamespace->snapshot();if(source&&*source){frame.internalNamespace=std::move(**source);}}
      std::erase_if(constraintPeerObservations,[&](const auto& value){return now-value.sampled>std::chrono::seconds(3);});
      frame.peerObservations.assign(constraintPeerObservations.begin(),constraintPeerObservations.end());
      if(engine){frame.managed=engine->managedDownloads();
        if(auto sample=engine->latest()){frame.peerEngineIdentity=sample->identity;}
        // Historical retired rows are not current provider observations. A
        // still-advertised nonzero tuple then fails closed as missing; a fresh
        // saved0/0 row may use only its actual held saved metadata.
        std::erase_if(frame.managed,[](const auto& value){return !value.retained;});
        if(coexistence()){auto queue=engine->externalQueueSnapshot();if(queue){frame.queue=std::move(*queue);}}
      }
      for(const auto& [hex,value]:i2pFacts){const auto hash=constraintFactHash(QString::fromStdString(hex));
        if(!hash){throw Failure{{storage::ErrorCode::Conflict,"Invalid current I2P constraint provider identity"}};}
        frame.i2p.push_back({*hash,value});
      }
      auto outcome=runConstraintPeerTurn(*constraintWorker,*manualTagStore,manualTagSnapshot,frame,engine.get(),stop);
      if(outcome&&outcome->assignmentRequest&&!stop.stop_requested()){
        if(frame.sequence==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"Assignment observation turns are exhausted"}};}
        ConstraintAssignmentObservation observation{*outcome->assignmentRequest,{}};
        try{
          const auto hash=constraintFactHash(observation.request.stamp.binding.target.hash);
          if(!hash){throw Failure{{storage::ErrorCode::Conflict,"Assignment request has no current download identity"}};}
          observation.value=tagAssignmentObservation(*hash,observation.request.add);
          if(!observation.request.rateTags.empty()){
            observation.rates=take(engine->prepareTagRateSource(lease,*manualTagStore,manualTagSnapshot,observation.request.rateTags));
          }

        }catch(const Failure& failure){
          if(failure.error.code!=storage::ErrorCode::Unsupported&&failure.error.code!=storage::ErrorCode::Busy){throw;}
          observation.retry=failure.error.code==storage::ErrorCode::Busy;
        }
        frame.assignmentObservation=std::move(observation);frame.assignmentOwner=tagAssignmentOwner.get();
        ++frame.sequence;frame.refreshSettings=false;
        outcome=runConstraintPeerTurn(*constraintWorker,*manualTagStore,manualTagSnapshot,frame,engine.get(),stop);
      }
      constraintSequence=frame.sequence;
      auto update=take(std::move(outcome));
      if(update.changed){
        // Do not add a post-commit cancellation checkpoint. The real save wins
        // and shared membership must reflect it before publishing statistics.
        constraintPeerObservations.clear();
        ++manualTagRevision;acceptManualTagSnapshot(std::move(update.snapshot),holder);
        if(update.assignmentReceipt){enqueueTagAssignment(std::move(*update.assignmentReceipt));}
        if(frame.assignmentObservation&&frame.assignmentObservation->rates){
          const auto note=applyAssignedTagRates();if(!note.isEmpty()){manualTagFailure=note;}
        }

        return true;
      }
      constraintPeerObservations.assign(frame.peerObservations.begin(),frame.peerObservations.end());
      return refreshed;
    }catch(const Failure& failure){
      if(failure.error.code==storage::ErrorCode::Cancelled||stop.stop_requested()){return false;}
      constraintPaused=true;
      manualTagFailure=SessionBridge::tr("Automatic tag updates paused (storage %1); reload tags before retrying")
          .arg(static_cast<int>(failure.error.code));
      return manualTagsEnabled;
    }catch(const std::bad_alloc&){
      constraintPaused=true;manualTagFailure=SessionBridge::tr("Automatic tag updates paused; reload tags before retrying");
      return manualTagsEnabled;
    }
  }
