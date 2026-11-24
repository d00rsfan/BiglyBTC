// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
QString applyDiscoveredTag(const rcm::TagScanSelection& selection,std::stop_token stop,bool& applied,bool& warned){
 const auto current=[&] {
  const auto snapshot=chats?chats->snapshot():nullptr;
  return snapshot&&snapshot->generation==columnLayoutSession&&!snapshot->closing&&!snapshot->closed&&
      snapshot->discoveries&&snapshot->discoveries->settings.enabled&&
      snapshot->swarmTagScan&&snapshot->swarmTagScan->contains(selection);
 };
 if(!normalNetwork||replacementReviewOnly||fileRecoveryRequired||manualTagEnded||
     !tagManagerAtOpen||!tagManagerAtOpen->enabled()||!tagManagerAtOpen->matches(lease)||!current()){
  throw Failure{{storage::ErrorCode::Conflict,"The selected Tag Discovery result is no longer current"}};
 }
 const auto hash=core::TorrentHash::parse(selection.hash);
 if(!hash||hash->bytes().size()!=20){
  throw Failure{{storage::ErrorCode::InvalidFormat,"The selected Tag Discovery torrent is invalid"}};
 }
 const auto key=hash->hex();
 if(recoveryDrainStarted||storage::hasPendingOriginalCompaction(lease)||
     storage::hasPendingTorrentIntake(lease)||storage::hasPendingCategoryDeletion(lease)||
     storage::hasPendingFileOperation(lease)||recheckTargets.contains(key)||pendingIntents.contains(key)||
     pendingTargets.contains(key)||privateImports.contains(key)||allocationPending.contains(key)||
     verifiedSeedPending.contains(key)){
  throw Failure{{storage::ErrorCode::Conflict,"The selected torrent has pending profile work"}};
 }
 if(!manualTagStore||!manualTagSnapshot.valid()){reloadManualTags(stop);}
 if(!manualTagSnapshot.writable()||manualTagRevision==std::numeric_limits<quint64>::max()){
  throw Failure{{storage::ErrorCode::Conflict,"The saved tag source requires review"}};
 }
 const auto& source=*manualTagSnapshot.tags();
 const auto selected=take(findSavedManualTag(source,tagManagerAtOpen->stockTagLocale(),selection.tag));
 const auto* manual=source.source->find("3");
 const auto* record=selected&&manual?manual->find(std::to_string(*selected)):nullptr;
 if(selected&&(!record||!classifyManualTagMembership(*record).addAllowed())){
  throw Failure{{storage::ErrorCode::Unsupported,"The existing tag does not accept manual membership"}};
 }
 std::optional<storage::TagAssignmentObservation> observation;
 std::optional<storage::PreparedTagRateSource> rates;
 std::optional<storage::ProfileControlPriority> priority;
 if(record&&!core::ordinarySavedTagMembershipEffects(source,*record)&&
     core::rateReadyLifecycleSavedTagMembershipEffects(source,*record)){
  priority.emplace(take(lease.prioritizeControls()));
  observation=tagAssignmentObservation(*hash,true,core::rateOnlySavedTagMembershipEffects(source,*record));
  rates=prepareAssignedTagRates(manualTagSnapshot,*selected);
 }
 auto holder=std::make_shared<storage::ManualTagSnapshot>();
 std::optional<storage::TagAssignmentReceipt> receipt;
 const auto now=QDateTime::currentSecsSinceEpoch();
 auto committed=take(publishTagChange([&]()->std::expected<storage::ManualTagCommit,storage::Error>{
  const auto access=lease.sourceAccess();
  if(!access){return std::unexpected(access.error());}
  if(!current()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The Tag Discovery selection changed"});}
  auto identity=storage::bindManualTagIdentity(*manualTagStore,lease,manualTagSnapshot,*hash);
  if(!identity){return std::unexpected(identity.error());}
  auto stateStore=storage::TorrentStateStore::open(lease);
  if(!stateStore){return std::unexpected(stateStore.error());}
  const auto ownerState=stateStore->loadAttributes(lease,*hash);
  if(!ownerState){return std::unexpected(ownerState.error());}
  const auto* attributes=ownerState->document().find("attributes");
  if(!attributes||classifyRoute(attributes->find("networks"))!=Route::Public){
   return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The torrent's Public source changed"});
  }
  if(!selected){return manualTagStore->createDiscoveryTag(manualTagSnapshot,*identity,selection.tag,now,stop);}
  if(observation){
   auto result=tagAssignmentOwner->change(*manualTagStore,manualTagSnapshot,*selected,*observation,true,now,stop,
       rates?&*rates:nullptr);
   if(!result){return std::unexpected(result.error());}
   receipt=std::move(result->receipt);return std::move(result->commit);
  }
  return manualTagStore->apply(manualTagSnapshot,core::SavedTagAddMember{*selected,*hash,now},stop);
 }));
 QString status=committed.changed?SessionBridge::tr("Tag assigned."):SessionBridge::tr("The torrent already has this tag.");
 applied=true;
 if(committed.changed){
  ++manualTagRevision;
  acceptManualTagSnapshot(std::move(committed.snapshot),holder);
  if(receipt){enqueueTagAssignment(std::move(*receipt));}
  auto warning=rates?applyAssignedTagRates():QString{};
  if(receipt&&warning.isEmpty()){warning=tickTagAssignmentActions(stop);}
  if(!warning.isEmpty()){warned=true;status=std::move(warning);}
 }
 if(tagSourcePublicationFailure){
  warned=true;
  return SessionBridge::tr("Tag saved. Transfer source requires review. %1").arg(text(tagSourcePublicationFailure->message));
 }
 return status;
}
#endif
// Called only by the existing worker that owns torrent admission.
bool applySwarmTagUpdates(std::stop_token stop) {
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
 if (!chats || !normalNetwork || replacementReviewOnly || fileRecoveryRequired ||
     recoveryDrainStarted || storage::hasPendingOriginalCompaction(lease) ||
     storage::hasPendingTorrentIntake(lease)) { return false; }

 bool registryChanged=false;
 for (const auto& update : chats->takeSwarmTagUpdates()) {
  if (stop.stop_requested()) { break; }
  const auto& hash=update.subject();
  const auto key=hash.hex();
  if (recheckTargets.contains(key) || pendingIntents.contains(key) || pendingTargets.contains(key) ||
      privateImports.contains(key) || allocationPending.contains(key) || verifiedSeedPending.contains(key)) {
   continue;
  }
  bool retainedPublicOwner=false;
  if (engine && engine->managedAvailable() && !i2p && !mixed) {
   const auto current=engine->managedDownloads();
   const auto retained=std::ranges::find_if(current,[&](const auto& row){return row.hash==hash && row.retained;});
   if (retained!=current.end()) {
    retainedPublicOwner=retained->admission && retained->checked && !retained->failure && !retained->recheck &&
        (retained->phase==engine::ManagedPhase::Downloading || retained->phase==engine::ManagedPhase::Seeding ||
         retained->phase==engine::ManagedPhase::Paused);
   }
  }
  if (!retainedPublicOwner && !subscriptionTargetAvailable(hash)) { continue; }

  const auto save=[&]() -> std::expected<void,storage::Error> {
   auto gate=lease.sourceAccess();
   if (!gate) { return std::unexpected(gate.error()); }
   auto inventory=storage::ConfigFile::load(lease,"downloads.config");
   if (!inventory || inventory->recoverySource()!=storage::RecoverySource::Primary) { return {}; }
   auto library=core::readLibrary(inventory->document());
   if (!library || !library->issues.empty()) { return {}; }
   const auto hasTarget=[&](const auto& row) { return row.hash && *row.hash==hash; };
   const auto target=std::ranges::find_if(library->downloads,hasTarget);
   if (target==library->downloads.end() || !target->persistent ||
       (!retainedPublicOwner && target->startup!=core::StartupState::Stopped) ||
       std::ranges::count_if(library->downloads,hasTarget)!=1) { return {}; }

   auto store=storage::TorrentStateStore::open(lease);
   if (!store) { return std::unexpected(store.error()); }
   auto state=store->loadAttributes(lease,hash);
   if (!state) { return std::unexpected(state.error()); }
   const auto* previous=state->document().find("attributes");
   if (!previous || classifyRoute(previous->find("networks"))!=Route::Public) { return {}; }
   auto attributes=update.encodeCurrentAttributes(lease,*state);
   if (!attributes) { return std::unexpected(attributes.error()); }
   if (!*attributes || stop.stop_requested()) { return {}; }
   auto saved=store->saveAttributes(lease,*state,std::move(**attributes));
   if (!saved) { return saved; }
   std::expected<void,storage::Error> rebase;
   if (retainedPublicOwner) {
    // Protect the buffered checkpoint after the original state commits.
    auto refreshed=engine->rebaseSwarmTags(lease,hash);
    if (!refreshed) { rebase=std::unexpected(std::move(refreshed.error())); }
    else if (!*refreshed) {
     rebase=std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The retained Swarm Tag owner changed"});
    }
   }

   if (!tagManagerAtOpen || !tagManagerAtOpen->enabled() ||
       manualTagRevision==std::numeric_limits<quint64>::max()) { return rebase; }
   try {
    if (!manualTagStore) { manualTagStore=take(storage::ManualTagStore::open(lease)); }
    auto holder=std::make_shared<storage::ManualTagSnapshot>();
    core::SavedTagObserveSwarm event{{update.tags().begin(),update.tags().end()},
        QDateTime::currentMSecsSinceEpoch(),{}};
    auto observed=take(manualTagStore->observeSwarmTags(lease,std::move(event),{}));
    if (observed.changed) {
     ++manualTagRevision;
     manualTagsEnabled=true;
     acceptManualTagSnapshot(std::move(observed.snapshot),holder);
     registryChanged=true;
    }
   } catch (const Failure&) {
    // The download state already contains the new list.
   } catch (const std::bad_alloc&) {
    // The download state already contains the new list.
   }
   return rebase;
  };
  static_cast<void>(publishTarget(hash,save));
 }
 return registryChanged;
#else
 static_cast<void>(stop);
 return false;
#endif
}
