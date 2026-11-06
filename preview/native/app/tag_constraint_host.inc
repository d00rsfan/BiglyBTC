// Included inside the existing worker-owned SessionBridge::Host. Never call
// provider methods from TagConstraintWorker's SAME-OFD source section.
  std::unique_ptr<TagConstraintWorker> constraintWorker;
  quint64 constraintSequence=0,constraintIntakeSequence=0;
  bool constraintPaused=false,constraintNormalReady=false;
  std::chrono::steady_clock::time_point constraintReloadAt{};
  void noteConstraintIntake(const core::TorrentHash& hash)noexcept{
    // Called ONLY from an actual successful fresh durable intake result. This
    // is not a UI/row/startup hook, and must not fail an already-saved import.
    if(!normalNetwork||!constraintNormalReady||replacementReviewOnly||fileRecoveryRequired||manualTagEnded||constraintPaused||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return;}
    try{
      if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession));}
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
  bool tickConstraintTags(std::stop_token stop){
    // Category-derived membership may be partial; do not persist rules from
    // it while ANY category journal candidate remains unresolved.
    if((storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return false;}
    if(!normalNetwork||!constraintNormalReady||replacementReviewOnly||fileRecoveryRequired||manualTagEnded
        ||constraintPaused||!lastSnapshot||lastSnapshot->revision!=revision||stop.stop_requested()){
      return false;
    }
    try{
      if(constraintSequence==std::numeric_limits<quint64>::max()){
        throw Failure{{storage::ErrorCode::Unsupported,"Automatic-tag worker turn is exhausted"}};
      }
      if(!constraintWorker){constraintWorker=take(TagConstraintWorker::activate(lease,columnLayoutSession));}
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
      if(engine){frame.managed=engine->managedDownloads();
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
      auto update=take(constraintWorker->step(*manualTagStore,manualTagSnapshot,frame,stop));
      if(update.changed){
        // Do not add a post-commit cancellation checkpoint. The real save wins
        // and shared membership must reflect it before publishing statistics.
        ++manualTagRevision;acceptManualTagSnapshot(std::move(update.snapshot),holder);return true;
      }
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
