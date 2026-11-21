// This code is compiled in the actual PersistentSession translation unit.
std::expected<CurrentTorrentDetails,storage::Error> detail::PersistentCall::queryCurrentTorrent(
    const PersistentSession& owner,const storage::ProfileLease& lease,const core::TorrentHash& hash){
  try{
    const auto& impl=*owner.impl_;
    detail::require(impl.onWorker(),"File details require the owning worker",storage::ErrorCode::Conflict);
    detail::require(!impl.closed&&!impl.poisoned&&!impl.closingAdmission&&!impl.call,"File-details owner is closing or unavailable",storage::ErrorCode::Conflict);
    struct stat profile{};detail::require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0&&S_ISDIR(profile.st_mode),"File details require a valid held profile",storage::ErrorCode::UnsafePath);
    detail::require(!impl.profileBound||(profile.st_dev==impl.profileDevice&&profile.st_ino==impl.profileInode),"File details cannot cross owner profiles",storage::ErrorCode::Conflict);
    detail::require(!impl.managedLease||lease.sharesOwnership(*impl.managedLease),"File-details profile ownership changed",storage::ErrorCode::Conflict);
    auto saved=engine::queryTorrent(lease,hash);if(!saved){return std::unexpected(saved.error());}
    const auto found=impl.managed.find(hash.hex());
    if(found==impl.managed.end()||!found->second->status.retained){return CurrentTorrentDetails{std::move(*saved),0};}
    const auto& live=*found->second;
    detail::require(live.status.admission&&live.token&&live.token->number==live.status.admission&&!live.stop&&!live.removing&&!live.status.failure&&live.status.phase!=ManagedPhase::Stopping&&live.handle.is_valid(),"Current file owner is retiring or failed",storage::ErrorCode::Busy);
    // AllocateOnly has no checking/ready file facade yet in the Source model.
    if(live.allocationIntent){return CurrentTorrentDetails{std::move(*saved),0};}
    const auto& layout=live.storage.capturedFileLayout();const auto policy=live.storage.capturedFilePolicy();
    detail::require(layout.files.size()==policy.size()&&layout.files.size()==saved->entries.size()&&layout.bytes==saved->bytes,"Current file census differs from saved metadata",storage::ErrorCode::Conflict);
    std::size_t pathBytes=0;
    for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];detail::require(file.path.size()<=32*1024*1024-pathBytes,"Current file paths exceed their query bound",storage::ErrorCode::Unsupported);pathBytes+=file.path.size();saved->entries[n]={static_cast<int>(n),file.path,file.bytes,policy[n].priority,policy[n].skipped,file.padding};}
    saved->name=layout.name;return CurrentTorrentDetails{std::move(*saved),live.status.admission};
  }catch(const detail::Failure& e){return std::unexpected(e.error);}catch(const std::exception&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot capture current file details"});}
}
std::expected<CurrentTorrentDetails,storage::Error> queryCurrentTorrent(
    const PersistentSession& owner,const storage::ProfileLease& lease,const core::TorrentHash& hash){return detail::PersistentCall::queryCurrentTorrent(owner,lease,hash);}
