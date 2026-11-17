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
  std::optional<storage::PreparedTagRateSource> prepareAssignedTagRates(const storage::ManualTagSnapshot& snapshot,std::int32_t id){
    const auto* tags=snapshot.tags();const auto* type=tags&&tags->source?tags->source->find("3"):nullptr;const auto* record=type?type->find(std::to_string(id)):nullptr;
    if(!record){throw Failure{{storage::ErrorCode::Conflict,"Rate tag source is no longer current"}};}
    auto rates=core::readSavedTagRateLimits(*record);if(!rates){throw Failure{{storage::ErrorCode::Unsupported,"Invalid saved tag rates"}};}
    if(rates->unlimited()){return std::nullopt;}
    activateTagAssignments();const std::array ids{id};
    return take(engine->prepareTagRateSource(lease,*manualTagStore,snapshot,ids));
  }
  QString applyAssignedTagRates(){
    const auto applied=engine->synchronizeSavedTagRates();
    return applied?QString{}:QStringLiteral("Membership saved;transfers held until saved tag rates recover: %1").arg(text(applied.error().message));
  }
  void enqueueTagAssignment(storage::TagAssignmentReceipt receipt){
    static_assert(std::is_nothrow_move_constructible_v<TagAssignmentWork>);
    // reserve32 and the capacity check happen before the real member CAS.
    tagAssignmentWork.push_back(TagAssignmentWork{std::move(receipt),{},0,{},false,{}});
  }
  bool assignLifecycleTagChip(const ui::TagChipAssignmentCommand& command,ui::TagChipAssignmentResult& result,std::stop_token stop){
    if(command.kind!=ui::TagChipAssignmentKind::Manual){return false;}
    if(!command.tagId||*command.tagId<0||command.category){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid manual tag selection"}};}
    // This passive hint only selects the normal-owner path. The actual store,
    // policy,identity and CAS are captured again below;ordinary/offline callers
    // keep the existing filesystem-only helper and retained source generation.
    auto hint=storage::ConfigFile::load(lease,"tag.config");if(!hint){return false;}
    auto hinted=core::readSavedDownloadTags(hint->document().originalBytes());if(!hinted){return false;}
    const auto* hintedType=hinted->source->find("3");const auto* hintedRecord=hintedType?hintedType->find(std::to_string(*command.tagId)):nullptr;
    if(!hintedRecord||core::ordinarySavedTagMembershipEffects(*hinted,*hintedRecord)||!core::rateReadyLifecycleSavedTagMembershipEffects(*hinted,*hintedRecord)){return false;}
    auto holder=std::make_shared<storage::ManualTagSnapshot>();
    if(!manualTagStore){manualTagStore=take(storage::ManualTagStore::open(lease,stop));*holder=take(manualTagStore->snapshot());}
    else{*holder=take(manualTagStore->reload(stop));}
    const auto* tags=holder->tags();const auto* type=tags&&tags->source?tags->source->find("3"):nullptr;
    const auto* record=type?type->find(std::to_string(*command.tagId)):nullptr;
    if(manualTagRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"Tag presentation revisions exhausted"}};}
    ++manualTagRevision;manualTagsEnabled=true;acceptManualTagSnapshot(*holder,holder);
    if(!record||!core::rateReadyLifecycleSavedTagMembershipEffects(*tags,*record)){throw Failure{{storage::ErrorCode::Conflict,"Selected action configuration changed;refresh tags"}};}
    if(command.targets.size()>32){throw Failure{{storage::ErrorCode::Unsupported,"Active tag actions support at most32 selected downloads per assignment"}};}
    const core::SavedDownloadTag* selected=nullptr;for(const auto& tag:tags->tags){if(tag.id==command.tagId){if(selected||tag.duplicateId){throw Failure{{storage::ErrorCode::Conflict,"Selected tag identity is ambiguous"}};}selected=&tag;}}
    if(!selected){throw Failure{{storage::ErrorCode::Missing,"Selected tag no longer exists"}};}
    std::vector<core::TorrentHash> hashes;hashes.reserve(command.targets.size());std::size_t matching=0;bool add=false;
    for(const auto& target:command.targets){auto hash=identity(target);if(std::ranges::find(hashes,hash)!=hashes.end()){throw Failure{{storage::ErrorCode::Conflict,"Duplicate assignment target"}};}hashes.push_back(hash);
      const bool member=std::ranges::find(selected->uniqueHashes,hash)!=selected->uniqueHashes.end();matching+=member;add|=!member;}
    const auto policy=classifyManualTagMembership(*record);
    if(policy.malformed||(command.longPress?policy.autoAdd:policy.autoAdd&&(add||policy.autoRemove))){throw Failure{{storage::ErrorCode::Unsupported,"Automatic tag membership cannot be changed in this direction"}};}
    activateTagAssignments();const auto required=add?hashes.size()-matching:0;
    if(required>32-tagAssignmentWork.size()){throw Failure{{storage::ErrorCode::Busy,"Assignment command queue cannot retain this complete selection"}};}
    auto priority=take(lease.prioritizeControls());std::vector<storage::TagAssignmentObservation> observations;observations.reserve(hashes.size());
    for(const auto& hash:hashes){observations.push_back(tagAssignmentObservation(hash,add));}
    auto rates=prepareAssignedTagRates(*holder,*command.tagId);
    const auto revisionBefore=manualTagRevision;
    if(revisionBefore==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"Tag presentation revisions exhausted"}};}
    auto changed=take(tagAssignmentOwner->changeSubjects(*manualTagStore,*holder,*command.tagId,observations,add,std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),stop,rates?&*rates:nullptr));
    // No post-CAS cancellation or allocation may discard an actual receipt.
    ++manualTagRevision;manualTagsEnabled=true;acceptManualTagSnapshot(std::move(changed.commit.snapshot),holder);
    result.applied=hashes.size();result.changed=changed.commit.changed?(add?hashes.size()-matching:matching):0;
    for(auto& receipt:changed.receipts){enqueueTagAssignment(std::move(receipt));}
    const auto rateNote=rates&&changed.commit.changed?applyAssignedTagRates():QString{};
    const auto note=tickTagAssignmentActions(stop);if(!rateNote.isEmpty()){result.failure=rateNote;}else if(!note.isEmpty()){result.failure=note;}
    return true;
  }
  QString tickTagAssignmentActions(std::stop_token stop){
    if(tagAssignmentWork.empty()){return {};}
    if(engine&&engine->tagRateFailure()){return QStringLiteral("Assignment actions await saved tag rate recovery");}
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
