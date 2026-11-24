// This code is compiled in the actual PersistentSession translation unit.
namespace {
std::optional<std::int64_t> verifiedWantedPieceBytes(const compat::OriginalLayout& layout,
    std::span<const compat::FilePolicy> policy, std::span<const std::uint8_t> verified) {
  const auto length = layout.pieceLength;
  if (length <= 0 || layout.bytes <= 0 || policy.size() != layout.files.size() ||
      static_cast<std::size_t>((layout.bytes - 1) / length + 1) != verified.size()) {
    return std::nullopt;
  }
  std::int64_t countedThrough = -1;
  std::int64_t result = 0;
  for (std::size_t index = 0; index < layout.files.size(); ++index) {
    const auto& file = layout.files[index];
    if (policy[index].skipped) { continue; }
    // The original piece map can contain an empty file. Its separate
    // last-piece builder includes trailing empty files for multi-piece torrents.
    if (file.bytes == 0 && file.offset == layout.bytes && verified.size() == 1) { continue; }
    const auto first = std::min(file.offset, layout.bytes - 1) / length;
    const auto last = file.bytes == 0 ? first : (file.offset + file.bytes - 1) / length;
    for (auto piece = std::max(countedThrough + 1, first); piece <= last; ++piece) {
      if (verified[static_cast<std::size_t>(piece)]) {
        result += std::min(length, layout.bytes - piece * length);
      }
    }
    countedThrough = std::max(countedThrough, last);
  }
  return result;
}
} // namespace

std::expected<CurrentTorrentDetails,storage::Error> detail::PersistentCall::queryCurrentTorrent(
    const PersistentSession& owner,const storage::ProfileLease& lease,const core::TorrentHash& hash,
    CurrentTorrentFileQuery query){
  try{
    const auto& impl=*owner.impl_;
    detail::require(impl.onWorker(),"File details require the owning worker",storage::ErrorCode::Conflict);
    detail::require(!impl.closed&&!impl.poisoned&&!impl.closingAdmission&&!impl.call,"File-details owner is closing or unavailable",storage::ErrorCode::Conflict);
    struct stat profile{};detail::require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0&&S_ISDIR(profile.st_mode),"File details require a valid held profile",storage::ErrorCode::UnsafePath);
    detail::require(!impl.profileBound||(profile.st_dev==impl.profileDevice&&profile.st_ino==impl.profileInode),"File details cannot cross owner profiles",storage::ErrorCode::Conflict);
    detail::require(!impl.managedLease||lease.sharesOwnership(*impl.managedLease),"File-details profile ownership changed",storage::ErrorCode::Conflict);
    const auto found = impl.managed.find(hash.hex());
    const bool retained = found != impl.managed.end() && found->second->status.retained;
    auto saved = engine::queryTorrent(lease, hash,
        {query.fileStatistics && !retained, query.rateLimits && !retained});
    if (!saved) { return std::unexpected(saved.error()); }
    if (!retained) { return CurrentTorrentDetails{std::move(*saved), 0, {}}; }
    const auto& live=*found->second;
    detail::require(live.status.admission&&live.token&&live.token->number==live.status.admission&&!live.stop&&!live.removing&&!live.status.failure&&live.status.phase!=ManagedPhase::Stopping&&live.handle.is_valid(),"Current file owner is retiring or failed",storage::ErrorCode::Busy);
    // AllocateOnly has no checking/ready file facade yet in the Source model.
    if(live.allocationIntent){return CurrentTorrentDetails{std::move(*saved),0,{}};}
    const auto& layout=live.storage.capturedFileLayout();const auto policy=live.storage.capturedFilePolicy();
    detail::require(layout.files.size()==policy.size()&&layout.files.size()==saved->entries.size()&&layout.bytes==saved->bytes,"Current file census differs from saved metadata",storage::ErrorCode::Conflict);
    std::size_t pathBytes=0;
    for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];detail::require(file.path.size()<=32*1024*1024-pathBytes,"Current file paths exceed their query bound",storage::ErrorCode::Unsupported);pathBytes+=file.path.size();saved->entries[n]={static_cast<int>(n),file.path,file.bytes,policy[n].priority,policy[n].skipped,file.padding};}
    saved->name = layout.name;
    if (query.rateLimits) {
      saved->rateLimits = TorrentRateLimits{std::max(0, live.handle.download_limit()),
          std::max(0, live.handle.upload_limit())};
    }
    std::optional<SelectedTorrentProgress> selected;
    std::optional<std::int64_t> verifiedWanted;
    if ((query.progress || query.fileStatistics || query.verifiedWantedPieces) && live.status.checked &&
        live.status.totalPieces > 0 && live.status.totalPieces <= 2000000) {
      const auto sample = live.handle.status(lt::torrent_handle::query_pieces);
      if (sample.state != lt::torrent_status::checking_files &&
          sample.state != lt::torrent_status::checking_resume_data &&
          sample.pieces.size() == live.status.totalPieces) {
        std::vector<std::uint8_t> verified(static_cast<std::size_t>(live.status.totalPieces));
        for (int index = 0; index < sample.pieces.size(); ++index) {
          verified[static_cast<std::size_t>(index)] = sample.pieces[lt::piece_index_t{index}] ? 1 : 0;
        }
        if (query.verifiedWantedPieces) { verifiedWanted = verifiedWantedPieceBytes(layout, policy, verified); }
        const auto fileProgress = query.progress || query.fileStatistics
            ? live.storage.inspectFileProgress(verified) : std::vector<std::int64_t>{};
        if ((query.progress || query.fileStatistics) && fileProgress.size() == saved->entries.size()) {
          if (query.progress) { selected = SelectedTorrentProgress{}; }
          if (query.fileStatistics) { saved->fileStatistics.emplace().reserve(fileProgress.size()); }
          for (std::size_t index = 0; index < fileProgress.size(); ++index) {
            const auto& file = saved->entries[index];
            if (fileProgress[index] < 0 || fileProgress[index] > file.bytes) {
              selected.reset();
              saved->fileStatistics.reset();
              break;
            }
            if (saved->fileStatistics) {
              const auto deleted = live.storage.fileDeleted(index);
              if (!deleted) { saved->fileStatistics.reset(); }
              else { saved->fileStatistics->push_back({fileProgress[index], !file.skipped && !*deleted, file.priority}); }
            }
            if (selected && !file.skipped) {
              selected->size += file.bytes;
              selected->remaining += file.bytes - fileProgress[index];
            }
          }
        }
      }
    }
    return CurrentTorrentDetails{std::move(*saved), live.status.admission, selected, verifiedWanted};
  }catch(const detail::Failure& e){return std::unexpected(e.error);}catch(const std::exception&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot capture current file details"});}
}
std::expected<CurrentTorrentDetails,storage::Error> queryCurrentTorrent(
    const PersistentSession& owner,const storage::ProfileLease& lease,const core::TorrentHash& hash,
    CurrentTorrentFileQuery query){return detail::PersistentCall::queryCurrentTorrent(owner,lease,hash,query);}
