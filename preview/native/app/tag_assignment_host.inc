// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside the actual serialized Host;no separate timer or source owner.
  std::expected<void,storage::Error> executePublicLibraryCommand(const core::TorrentHash& hash,core::DownloadAction action,bool controllerQueue=false){
    const bool mixedRow=route(hash)==Route::Mixed;
    if(mixedRow&&(action==core::DownloadAction::Stop||action==core::DownloadAction::Pause)){
      const auto removed=removeMixed(hash);if(!removed){return std::unexpected(removed.error());}
    }
    const auto queued=[&]()->std::expected<storage::LibraryCommandResult,storage::Error>{
      std::optional<storage::LibraryCommandResult> result;
      auto done=publishTarget(hash,[&]()->std::expected<void,storage::Error>{auto value=storage::applyLibraryControllerQueue(lease,hash);if(!value){return std::unexpected(value.error());}result=std::move(*value);return {};});
      if(!done){return std::unexpected(done.error());}return std::move(*result);
    };
    auto changed=controllerQueue?queued():libraryCommand(hash,action,movePools);if(!changed){return std::unexpected(changed.error());}
    admissionFailures.erase(hash.hex());
    std::expected<void,storage::Error> controlled;
    auto states=engine->managedDownloads();
    const auto live=std::ranges::find_if(states,[&](const auto& value){return value.hash==hash;});
    const bool active=live!=states.end()&&live->retained;
    switch(action){
    case core::DownloadAction::Stop:controlled=engine->stopManaged(hash);break;
    case core::DownloadAction::CancelForceStart:if(active){controlled=engine->clearManagedForceStart(hash);}break;
    case core::DownloadAction::Pause:if(active){controlled=engine->pauseManaged(hash,true);}break;
    case core::DownloadAction::Start:case core::DownloadAction::Resume:
    case core::DownloadAction::Queue:case core::DownloadAction::ForceStart:
      if(changed->pauseAfter||changed->after.startup==core::StartupState::Stopped){break;}
      if(active){controlled=engine->queueManaged(hash,changed->after.forceStart,changed->after.startup==core::StartupState::Queued);}
      else{controlled=startPublic({hash,peerPort,changed->after.forceStart,changed->after.startup==core::StartupState::Queued});}
      break;
    default:{
      auto source=take(storage::readLibraryView(lease));
      std::ranges::stable_sort(source.items,{},[](const auto& item){return item.saved.position.value_or(0);});
      std::vector<core::TorrentHash> order;for(const auto& item:source.items){if(item.saved.hash){order.push_back(*item.saved.hash);}}
      controlled=engine->reorderManaged(order);break;
    }
    }
    if(!controlled){admissionFailures.insert_or_assign(hash.hex(),controlled.error());return controlled;}
    if(mixedRow&&active&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)
        &&(!mixedStatus.contains(hash.hex())||!mixedStatus.at(hash.hex()).active)){retryMixed(hash);}
    static_cast<void>(engine->rearmMainlineScrape(hash));return {};
  }
  struct TagAssignmentWork {
    storage::TagAssignmentReceipt receipt;
    std::optional<storage::TagAssignmentCommands> current;
    unsigned effect=0;
    std::optional<std::uint64_t> retiring;
    bool requeue=false;
    std::chrono::steady_clock::time_point deadline{};
  };
  std::unique_ptr<storage::TagAssignmentOwner> tagAssignmentOwner;
  std::vector<TagAssignmentWork> tagAssignmentWork;
  void activateTagAssignments(){
    if(!normalNetwork||!constraintNormalReady||!tagManagerAtOpen||!tagManagerAtOpen->enabled()||!engine||!engine->managedAvailable()
        ||replacementReviewOnly||fileRecoveryRequired||recoveryDrainStarted||manualTagEnded){
      throw Failure{{storage::ErrorCode::Unsupported,"Assignment actions require an active normal profile owner"}};
    }
    if(!tagAssignmentOwner){tagAssignmentWork.reserve(32);tagAssignmentOwner=take(storage::TagAssignmentOwner::activate(lease,columnLayoutSession,*tagManagerAtOpen));}
    if(tagAssignmentWork.size()>=32){throw Failure{{storage::ErrorCode::Busy,"Assignment command queue is full"}};}
  }
  storage::TagAssignmentObservation tagAssignmentObservation(const core::TorrentHash& hash,bool add){
    activateTagAssignments();
    if(route(hash)!=Route::Public||pendingTargets.contains(hash.hex())||pendingIntents.contains(hash.hex())
        ||verifiedSeedPending.contains(hash.hex())||allocationPending.contains(hash.hex())||recheckTargets.contains(hash.hex())
        ||storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease)||storage::hasPendingOriginalCompaction(lease)){
      throw Failure{{storage::ErrorCode::Unsupported,"Assignment actions await a supported current Public download owner"}};
    }
    // Provider capture occurs BEFORE the caller obtains its member/source gate.
    const auto states=add?engine->managedDownloads():std::vector<engine::ManagedDownloadStatus>{};
    const engine::ManagedDownloadStatus* live=nullptr;
    for(const auto& value:states){if(value.hash==hash&&value.retained){if(live){throw Failure{{storage::ErrorCode::Conflict,"Assignment provider is ambiguous"}};}live=&value;}}
    std::int32_t state=0;
    if(live){
      using P=engine::ManagedPhase;
      switch(live->phase){
      case P::Downloading:state=50;break;case P::Seeding:state=60;break;case P::Queued:state=75;break;
      case P::Stopped:case P::Paused:state=70;break;case P::Failed:state=100;break;
      case P::Checking:
        if(live->verificationHeld||live->recheck){throw Failure{{storage::ErrorCode::Busy,"Assignment actions await the explicit verification owner"}};}
        state=30;break;
      case P::Allocating:case P::Stopping:
        throw Failure{{storage::ErrorCode::Busy,"Assignment actions await the current data transition"}};
      }
    }else if(add){
      const auto source=take(storage::readLibraryView(lease));const storage::LibraryItem* selected=nullptr;
      for(const auto& item:source.items){if(item.saved.hash&&*item.saved.hash==hash){if(selected){throw Failure{{storage::ErrorCode::Conflict,"Assignment download is ambiguous"}};}selected=&item;}}
      if(!selected){throw Failure{{storage::ErrorCode::Missing,"Assignment download is no longer present"}};}
      state=admissionFailures.contains(hash.hex())?100:selected->saved.startup==core::StartupState::Stopped?70:selected->saved.startup==core::StartupState::Queued?75:0;
    }
    return {hash,columnLayoutSession,lease.sourceRevision(),state,live?live->admission:0,live?columnLayoutSession:0};
  }
  void enqueueTagAssignment(storage::TagAssignmentReceipt receipt){
    static_assert(std::is_nothrow_move_constructible_v<TagAssignmentWork>);
    // reserve32 and the capacity check happen before the real member CAS.
    tagAssignmentWork.push_back(TagAssignmentWork{std::move(receipt),{},0,{},false,{}});
  }
  QString tickTagAssignmentActions(std::stop_token stop){
    if(tagAssignmentWork.empty()){return {};}
    try{
      if(stop.stop_requested()||!tagAssignmentOwner||!manualTagStore||!engine||!normalNetwork){tagAssignmentWork.clear();return {};}
      for(unsigned budget=0;budget<4&&!tagAssignmentWork.empty();++budget){
        auto& work=tagAssignmentWork.front();
        if(!work.current){
          if(work.receipt.done()){tagAssignmentWork.erase(tagAssignmentWork.begin());continue;}
          work.current=take(tagAssignmentOwner->next(*manualTagStore,manualTagSnapshot,work.receipt,stop));work.effect=0;
        }
        const auto& hash=work.current->hash;
        if(route(hash)!=Route::Public||fileRecoveryRequired||recoveryDrainStarted||storage::hasPendingCategoryDeletion(lease)
            ||storage::hasPendingFileOperation(lease)||storage::hasPendingOriginalCompaction(lease)){
          throw Failure{{storage::ErrorCode::Conflict,"Assignment command source requires review"}};
        }
        while(work.effect<work.current->commands.count){
          const auto effect=work.current->commands.values[work.effect];
          using E=core::tags::assignment::Effect;
          if(effect==E::CheckMaximum){++work.effect;continue;} // Strict source has no maximum-count effects.
          if(effect==E::QueueAfterStop||effect==E::Stop){
            const auto states=engine->managedDownloads();const engine::ManagedDownloadStatus* live=nullptr;
            for(const auto& value:states){if(value.hash==hash&&value.retained){live=&value;break;}}
            if(!work.retiring){
              storage::LibraryControllerState observed;
              if(live){
                using P=engine::ManagedPhase;
                observed.state=live->phase==P::Stopping?65:live->phase==P::Failed?100:
                    (live->phase==P::Stopped||live->phase==P::Paused)?70:live->phase==P::Queued?75:
                    live->phase==P::Seeding?60:live->phase==P::Downloading?50:30;
                observed.diskManagerPresent=live->phase!=P::Stopped&&live->phase!=P::Paused;
              }else{
                const auto source=take(storage::readLibraryView(lease));
                const auto selected=std::ranges::find_if(source.items,[&](const auto& item){return item.saved.hash&&*item.saved.hash==hash;});
                if(selected==source.items.end()){throw Failure{{storage::ErrorCode::Missing,"Assignment target disappeared"}};}
                observed.state=admissionFailures.contains(hash.hex())?100:static_cast<std::int32_t>(selected->saved.startup);
              }
              const auto target=effect==E::QueueAfterStop?storage::LibraryControllerStopTarget::Queued:storage::LibraryControllerStopTarget::Stopped;
              take(publishTarget(hash,[&]()->std::expected<void,storage::Error>{auto saved=storage::applyLibraryControllerStop(lease,hash,target,observed);if(!saved){return std::unexpected(saved.error());}return {};}));
              admissionFailures.erase(hash.hex());
              work.requeue=effect==E::QueueAfterStop&&observed.state!=65;
              if(live){
                work.retiring=live->admission;work.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1000);
                take(engine->stopManaged(hash));return {};
              }
            }
            if(work.retiring&&live&&live->admission==*work.retiring){
              if(std::chrono::steady_clock::now()>=work.deadline){throw Failure{{storage::ErrorCode::Busy,"Assignment retirement did not complete"}};}
              return {};
            }
            if(work.requeue&&!live){take(executePublicLibraryCommand(hash,core::DownloadAction::Queue));}
            work.retiring.reset();work.requeue=false;
          }else{
            const auto action=effect==E::Queue?core::DownloadAction::Queue:effect==E::Resume?core::DownloadAction::Resume:
                effect==E::Stop?core::DownloadAction::Stop:effect==E::Pause?core::DownloadAction::Pause:
                effect==E::ForceStart?core::DownloadAction::ForceStart:core::DownloadAction::CancelForceStart;
            take(executePublicLibraryCommand(hash,action,effect==E::Queue));
          }
          ++work.effect;
        }
        work.current.reset();
        coexistQueue.invalidatePositions();scheduleCoexistence();
        if(work.receipt.done()){tagAssignmentWork.erase(tagAssignmentWork.begin());}
      }
      return {};
    }catch(const Failure& error){tagAssignmentWork.erase(tagAssignmentWork.begin());return SessionBridge::tr("Tag assigned; its queued action did not complete: %1").arg(text(error.error.message));}
    catch(...){tagAssignmentWork.erase(tagAssignmentWork.begin());return SessionBridge::tr("Tag assigned; its queued action did not complete. Review the download before retrying.");}
  }
