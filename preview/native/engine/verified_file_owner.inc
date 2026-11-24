// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the owning session. The payload worker never sees an SDK handle.
struct VerifiedReadRequest {
  detail::MergeIoOwner owner;
  std::optional<detail::MergeIoData> result;
};
detail::FileMergeIo fileReadIo;
std::map<std::uint64_t,VerifiedReadRequest> verifiedReads;
std::uint64_t nextVerifiedRead=0;
struct FileChannelPriorityWindow {
  std::uint64_t admission=0;
  std::size_t file=0;
  std::uint64_t offset=0;
  std::map<int,lt::download_priority_t> originalPriorities;
};
std::map<std::string,FileChannelPriorityWindow> fileChannelWindows;

Managed* verifiedReader(const detail::MergeIoOwner& owner) {
  if(!onWorker()||closed||poisoned||call||closingAdmission||storageError||transferError||
      removalLost||!managedLease||!managedLease->valid()){
    return nullptr;
  }
  auto* value=mergeCurrent(owner);
  if(!value||!value->token||value->token->number!=owner.admission||!value->status.retained||
      !value->status.checked||value->status.failure||value->status.recheck||value->stop||
      value->removing||value->allocationIntent||value->status.verificationHeld||!value->handle.is_valid()){
    return nullptr;
  }
  return value;
}

std::expected<const compat::OriginalFile*, storage::Error> verifiedFile(
    const PersistentSession::VerifiedFileSelection& request) {
  using Code = storage::ErrorCode;
  const auto* value = request.engine == identity ?
      verifiedReader({request.hash.hex(), request.admission}) : nullptr;
  if (!value) {
    return std::unexpected(storage::Error{Code::Conflict, "Current verified file admission is unavailable"});
  }
  const auto& files = value->storage.capturedFileLayout().files;
  const auto policy = value->storage.capturedFilePolicy();
  if (request.file >= files.size() || request.file >= policy.size() || files[request.file].padding ||
      files[request.file].bytes < 0 || policy[request.file].skipped) {
    return std::unexpected(storage::Error{Code::InvalidFormat, "The HTTP media file selection is unavailable"});
  }
  return &files[request.file];
}

void collectVerifiedFileReads() {
  for(auto& result:fileReadIo.take()){
    if(const auto found=verifiedReads.find(result.id);found!=verifiedReads.end()){
      found->second.result=std::move(result.data);
    }
  }
}

void cancelVerifiedFileReads(const Managed& value) {
  const auto owner=mergeOwner(value);
  fileReadIo.cancelOwner(owner);
  std::erase_if(verifiedReads,[&](const auto& request){return request.second.owner==owner;});
}

static void restoreFileChannelPriority(const Managed& value,int piece,lt::download_priority_t prior) {
  const auto index=lt::piece_index_t{piece};
  value.handle.reset_piece_deadline(index);
  value.handle.piece_priority(index,prior);
}

bool clearFileChannelWindow(const Managed& value) noexcept {
  bool restored=true;
  try{
    const auto found=fileChannelWindows.find(value.status.hash.hex());
    if(found==fileChannelWindows.end()||found->second.admission!=value.status.admission){return true;}
    if(value.handle.is_valid()){
      for(const auto& [piece,prior]:found->second.originalPriorities){
        try{restoreFileChannelPriority(value,piece,prior);}catch(...){restored=false;}
      }
    }
    fileChannelWindows.erase(found);
  }catch(...){restored=false;}
  return restored;
}

void stopPayloadReaders() noexcept {
  for(const auto& [hash,window]:fileChannelWindows){
    const auto found=managed.find(hash);
    if(found==managed.end()||found->second->status.admission!=window.admission||
        !found->second->handle.is_valid()){continue;}
    for(const auto& [piece,prior]:window.originalPriorities){
      try{restoreFileChannelPriority(*found->second,piece,prior);}catch(...){}
    }
  }
  fileChannelWindows.clear();
  mergeIo.stopAndJoin();
  fileReadIo.stopAndJoin();
  verifiedReads.clear();
}
