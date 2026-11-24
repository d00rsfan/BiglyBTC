// SPDX-License-Identifier: GPL-2.0-or-later
bool PersistentSession::verifiedFileSourceCurrent(
    const AccountingIdentity& identity, const core::TorrentHash& hash, std::uint64_t admission) const {
  return identity == impl_->identity && impl_->verifiedReader({hash.hex(), admission});
}

std::expected<std::uint64_t, storage::Error> PersistentSession::verifiedFileSize(
    const VerifiedFileSelection& selection) const {
  const auto file = impl_->verifiedFile(selection);
  if (!file) {
    return std::unexpected(file.error());
  }
  return static_cast<std::uint64_t>((*file)->bytes);
}

std::expected<PersistentSession::VerifiedFileSelection, storage::Error> PersistentSession::selectVerifiedFile(
    const core::TorrentHash& hash, std::size_t file) const {
  using Code = storage::ErrorCode;
  if (!impl_->onWorker()) {
    return std::unexpected(storage::Error{Code::Conflict, "Verified file selection requires its engine worker"});
  }
  const auto current = impl_->managed.find(hash.hex());
  if (current == impl_->managed.end()) {
    return std::unexpected(storage::Error{Code::Missing, "No current verified torrent admission exists"});
  }
  VerifiedFileSelection selected{impl_->identity, hash, current->second->status.admission, file};
  if (const auto valid = verifiedFileSize(selected); !valid) {
    return std::unexpected(valid.error());
  }
  return selected;
}

std::expected<PersistentSession::VerifiedFileInfo, storage::Error> PersistentSession::verifiedFileInfo(
    const VerifiedFileSelection& selection) const {
  const auto file = impl_->verifiedFile(selection);
  if (!file) {
    return std::unexpected(file.error());
  }
  const std::string& path = (*file)->path;
  const auto separator = path.find_last_of('/');
  return VerifiedFileInfo{static_cast<std::uint64_t>((*file)->bytes),
      separator == path.npos ? path : path.substr(separator + 1)};
}

std::expected<PersistentSession::VerifiedFileReadTicket,storage::Error> PersistentSession::requestVerifiedFileRead(
    const core::TorrentHash& hash,std::uint64_t admission,std::size_t file,
    std::uint64_t offset,std::uint32_t maximum) {
  auto& engine=*impl_;
  const detail::MergeIoOwner owner{hash.hex(),admission};
  auto* value=engine.verifiedReader(owner);
  if(!value){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Current verified file admission is unavailable"});
  }
  // Bound unread data as well as pending work. One request per admission lets
  // a seek cancel a read without touching another torrent or swarm merge.
  if(engine.verifiedReads.size()>=8||engine.fileReadIo.ownerBusy(owner)||
      std::ranges::any_of(engine.verifiedReads,[&](const auto& job){return job.second.owner==owner;})){
    return std::unexpected(storage::Error{storage::ErrorCode::Busy,"A verified file read is still pending"});
  }
  if(engine.nextVerifiedRead==std::numeric_limits<std::uint64_t>::max()){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Verified file read tickets are exhausted"});
  }
  try{
    // Release reservations left by a prior explicit seek or retirement.
    engine.collectVerifiedFileReads();
    auto read=value->storage.prepareVerifiedFileRead(*engine.managedLease,file,offset,maximum);
    if(!read){return std::unexpected(read.error());}
    const auto id=++engine.nextVerifiedRead;
    auto pending=engine.verifiedReads.emplace(id,Impl::VerifiedReadRequest{owner,{}}).first;
    const auto execute=[read=std::move(*read)](std::stop_token stop) mutable->detail::MergeIoData{
      auto data=read(stop);
      if(data){return std::move(*data);}
      using Code=storage::ErrorCode;
      using Failure=detail::MergeIoFailure;
      const auto& error=data.error();
      const auto code=error.code==Code::Cancelled?Failure::Cancelled:
          error.code==Code::Missing?Failure::Hash:Failure::Read;
      return std::unexpected(Failure{code,error.message});
    };
    constexpr auto reserved=16*1024*1024+detail::ManagedStorage::MaximumVerifiedFileChunk;
    try{
      if(engine.fileReadIo.submit({id,{owner},reserved,std::move(execute)})){
        return VerifiedFileReadTicket{engine.identity,id};
      }
    }catch(...){
      engine.verifiedReads.erase(pending);
      throw;
    }
    engine.verifiedReads.erase(pending);
    return std::unexpected(storage::Error{storage::ErrorCode::Busy,"Verified file read budget is full"});
  }catch(...){
    return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot schedule a verified file read"});
  }
}

std::expected<std::optional<std::vector<char>>,storage::Error> PersistentSession::takeVerifiedFileRead(
    const VerifiedFileReadTicket& ticket,const core::TorrentHash& hash,std::uint64_t admission) {
  auto& engine=*impl_;
  const detail::MergeIoOwner owner{hash.hex(),admission};
  if(ticket.engine!=engine.identity||!engine.verifiedReader(owner)){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Verified file read admission is no longer current"});
  }
  const auto pending=engine.verifiedReads.find(ticket.request);
  if(pending==engine.verifiedReads.end()||pending->second.owner!=owner){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Verified file read ticket is unavailable"});
  }
  engine.collectVerifiedFileReads();
  if(!pending->second.result){return std::nullopt;}
  auto result=std::move(*pending->second.result);
  engine.verifiedReads.erase(pending);
  if(result){return std::optional<std::vector<char>>(std::move(*result));}
  const auto& error=result.error();
  using Failure=detail::MergeIoFailure;
  using Code=storage::ErrorCode;
  const auto code=error.code==Failure::Cancelled?Code::Cancelled:
      error.code==Failure::Hash?Code::Missing:Code::Io;
  return std::unexpected(storage::Error{code,error.message});
}

bool PersistentSession::cancelVerifiedFileRead(
    const VerifiedFileReadTicket& ticket,const core::TorrentHash& hash,std::uint64_t admission) {
  auto& engine=*impl_;
  if(!engine.onWorker()||ticket.engine!=engine.identity){return false;}
  const detail::MergeIoOwner owner{hash.hex(),admission};
  const auto pending=engine.verifiedReads.find(ticket.request);
  if(pending==engine.verifiedReads.end()||pending->second.owner!=owner){return false;}
  engine.fileReadIo.cancelOwner(owner);
  engine.verifiedReads.erase(pending);
  return true;
}

std::expected<PersistentSession::FileChannelWindowStatus,storage::Error>
PersistentSession::seekFileChannel(const FileChannelSeek& request) {
  constexpr std::uint32_t maxScheduled=128;
  constexpr std::size_t maxWindows=8;
  using Code=storage::ErrorCode;
  auto& engine=*impl_;
  const detail::MergeIoOwner owner{request.hash.hex(),request.admission};
  auto* value=engine.verifiedReader(owner);
  if(!value||request.engine!=engine.identity||engine.tagRateProblem||value->manualPaused||value->policyHeld||
      (value->status.phase!=ManagedPhase::Downloading&&value->status.phase!=ManagedPhase::Seeding)){
    return std::unexpected(storage::Error{Code::Conflict,"File channel admission is unavailable"});
  }
  const auto& layout=value->storage.capturedFileLayout();
  const auto policy=value->storage.capturedFilePolicy();
  if(request.file>=layout.files.size()||request.file>=policy.size()||
      layout.files[request.file].padding||policy[request.file].skipped||
      request.offset>std::uint64_t(layout.files[request.file].bytes)||layout.pieceLength<=0||
      value->status.totalPieces<=0){
    return std::unexpected(storage::Error{Code::InvalidFormat,"File channel range or selection is unavailable"});
  }
  const auto& file=layout.files[request.file];
  const auto plan=core::fileChannelPieceWindow(file.offset,request.offset,
      std::uint32_t(layout.pieceLength),std::uint32_t(value->status.totalPieces),
      request.bytesPerSecond,request.bufferMilliseconds,request.minimumPieces,request.delayMilliseconds);
  if(!plan){return std::unexpected(storage::Error{Code::InvalidFormat,"File channel deadline window is out of range"});}
  if(request.offset==std::uint64_t(file.bytes)){
    if(!engine.clearFileChannelWindow(*value)){
      return std::unexpected(storage::Error{Code::Io,"Cannot restore file channel priorities"});
    }
    engine.cancelVerifiedFileReads(*value);
    return FileChannelWindowStatus{plan->first,0,0};
  }
  const auto previous=engine.fileChannelWindows.find(owner.hash);
  if(previous==engine.fileChannelWindows.end()&&engine.fileChannelWindows.size()>=maxWindows){
    return std::unexpected(storage::Error{Code::Busy,"File channel window capacity is occupied"});
  }
  if(previous!=engine.fileChannelWindows.end()&&previous->second.admission!=owner.admission){
    return std::unexpected(storage::Error{Code::Conflict,"File channel admission changed"});
  }
  try{
    const auto existing=value->handle.get_piece_priorities();
    const auto sdk=value->handle.status(lt::torrent_handle::query_pieces);
    if((sdk.flags&lt::torrent_flags::paused)||existing.size()!=std::size_t(value->status.totalPieces)){
      return std::unexpected(storage::Error{Code::Conflict,"File channel piece priorities are unavailable"});
    }
    std::map<int,lt::download_priority_t> next;
    auto [at,created]=engine.fileChannelWindows.try_emplace(owner.hash,
        Impl::FileChannelPriorityWindow{owner.admission,request.file,request.offset,{}});
    auto& window=at->second;
    const auto& old=window.originalPriorities;
    for(std::uint32_t n=0;n<std::min(plan->count,maxScheduled);++n){
      const auto piece=int(plan->first+n);
      if(sdk.is_seeding||(piece<sdk.pieces.size()&&sdk.pieces[lt::piece_index_t{piece}])){continue;}
      const auto prior=old.find(piece);
      const auto priority=prior==old.end()?existing[std::size_t(piece)]:prior->second;
      if(priority!=lt::dont_download){next.emplace(piece,priority);}
    }
    if(!created&&(window.file!=request.file||window.offset!=request.offset)){
      engine.cancelVerifiedFileReads(*value);
    }
    for(const auto& [piece,prior]:old){
      if(!next.contains(piece)){Impl::restoreFileChannelPriority(*value,piece,prior);}
    }
    window.file=request.file;
    window.offset=request.offset;
    window.originalPriorities=std::move(next);
    for(const auto& [piece,prior]:window.originalPriorities){
      static_cast<void>(prior);
      const auto offset=std::uint64_t(std::uint32_t(piece)-plan->first)*plan->spacing;
      const auto delay=std::int64_t(plan->delay)+std::int64_t(offset);
      value->handle.set_piece_deadline(lt::piece_index_t{piece},int(delay));
    }
    const auto scheduled=std::uint32_t(window.originalPriorities.size());
    return FileChannelWindowStatus{plan->first,plan->count,scheduled};
  }catch(...){
    engine.clearFileChannelWindow(*value);
    return std::unexpected(storage::Error{Code::Io,"Cannot apply the file channel deadline window"});
  }
}

std::expected<void,storage::Error> PersistentSession::closeFileChannel(
    const AccountingIdentity& identity,const core::TorrentHash& hash,std::uint64_t admission) {
  auto& engine=*impl_;
  auto* value=engine.verifiedReader({hash.hex(),admission});
  if(!value||identity!=engine.identity){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"File channel admission changed"});
  }
  engine.cancelVerifiedFileReads(*value);
  if(!engine.clearFileChannelWindow(*value)){
    return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot restore file channel priorities"});
  }
  return {};
}
