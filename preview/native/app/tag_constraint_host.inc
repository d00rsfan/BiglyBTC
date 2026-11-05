// Included inside the existing worker-owned SessionBridge::Host. Never call
// provider methods from TagConstraintWorker's SAME-OFD source section.
  std::unique_ptr<TagConstraintWorker> constraintWorker;
  quint64 constraintSequence=0;
  bool constraintPaused=false,constraintNormalReady=false;
  std::chrono::steady_clock::time_point constraintReloadAt{};
  bool tickConstraintTags(std::stop_token stop){
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
