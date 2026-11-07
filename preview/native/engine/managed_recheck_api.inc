// Private implementation beside ordinary managed API members; no new executor.
std::expected<void,storage::Error> PersistentSession::startManagedRecheck(
    const storage::ProfileLease& lease,const core::TorrentHash& hash,std::stop_token stop){
  if(stop.stop_requested()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Read-only recheck cancelled before preparation"});}
  return startManagedImpl(lease,ManagedDownloadOptions{.hash=hash},{},true,stop);
}
std::expected<void,storage::Error> PersistentSession::cancelManagedRecheck(const core::TorrentHash& hash){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Recheck cancellation requires the current actual owner"});}
  const auto found=impl_->managed.find(hash.hex());
  if(found==impl_->managed.end()||!found->second->status.recheck){return std::unexpected(storage::Error{storage::ErrorCode::Missing,"No actual recheck admission"});}
  auto& value=*found->second;if(!value.status.retained){return {};}
  value.recheckCancelled=true;value.status.recheck->phase=ManagedRecheckPhase::Retiring;impl_->requestStop(value);return {};
}
std::expected<void,storage::Error> PersistentSession::clearManagedForceStart(const core::TorrentHash& hash){
  if(!managedAvailable()||impl_->call){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Managed force control unavailable"});}
  if(!impl_->bindingHealthy()){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Actual network binding was lost"});}
  const auto found=impl_->managed.find(hash.hex());
  if(found==impl_->managed.end()||!found->second->handle.is_valid()||found->second->stop||found->second->status.recheck){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent has no ordinary live force controller"});}
  auto& value=*found->second;value.forceStart=false;value.status.forceStart=false;impl_->externalSnapshotValid=false;
  // Unlike queueManaged(false), never clears manualPaused or explicitly
  // resumes a user-held member. Only the existing ordinary queue may act.
  if(value.manualPaused){value.handle.unset_flags(lt::torrent_flags::auto_managed);}
  else if(!impl_->externalQueue&&!impl_->queuePolicy&&!value.seedIntent){value.handle.set_flags(lt::torrent_flags::auto_managed);}
  impl_->sampleManaged(value);if(impl_->queuePolicy&&!impl_->externalQueue){impl_->applyQueue();}return {};
}
