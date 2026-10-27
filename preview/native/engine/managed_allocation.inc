// Included by managed_storage.cpp after Impl. Uses only the held worker owner.
namespace {
template<class Owner> void allocationStateCurrent(Owner& owner,const storage::ProfileLease& lease){
  struct stat profile{};require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0
      &&profile.st_dev==owner.profileDevice&&profile.st_ino==owner.profileInode,"Allocation lost its held profile",ErrorCode::Conflict);
  const auto fresh=take(owner.store.load(lease,owner.hash));const auto source=authoritative(lease,fresh);
  require(source.serialize()==owner.state.document().serialize(),"Original allocation state or authoritative cached attributes changed",ErrorCode::Conflict);
  const auto identity=take(storage::originalTorrentIdentityFromLibtorrent(source,*owner.params.ti));
  require(take(storage::compareOriginalTorrentIdentities(owner.selectedIdentity,identity))==storage::OriginalIdentityRelation::SameSelected
      &&take(storage::originalQueueMappingScope(source))==owner.queueMappingScope,"Allocation identity or physical mapping changed",ErrorCode::Conflict);
  const auto* attributes=source.find("attributes");const auto flags=core::originalDownloadFlags(attributes);
  require(attributes&&integer(attributes->find("fas"),-1)==2&&flags&&((*flags&(0x200|0x4000))==0),"Allocation flags changed",ErrorCode::Conflict);
}
}
ManagedAllocationProgress ManagedStorage::allocationProgress() const noexcept {
  if(!impl_||!impl_->allocation){return {};}
  const auto& work=*impl_->allocation;return {work.completed,work.total,work.cursor==work.files.size()};
}
std::expected<void,Error> ManagedStorage::beginAllocation(const storage::ProfileLease& lease){
  try{
    require(impl_&&impl_->purpose==ManagedStoragePurpose::AllocateOnly&&impl_->allocation,"No writable allocation purpose",ErrorCode::Unsupported);
    auto& owner=*impl_;auto& work=*owner.allocation;require(!work.started,"Allocation was already initialized",ErrorCode::Conflict);work.started=true;allocationStateCurrent(owner,lease);
    AllocationDirectories complete;
    for(const auto& file:work.files){const auto current=allocationChain(file.path.parent_path(),false);for(const auto& [path,id]:current){const auto old=work.directories.find(path);require(old==work.directories.end()||old->second==id,"Allocation approved ancestor changed before journal",ErrorCode::Conflict);require(complete.emplace(path,id).second||complete.at(path)==id,"Allocation directory aliases changed",ErrorCode::Conflict);}}
    require(complete.size()<=8192&&std::ranges::all_of(work.directories,[&](const auto& value){const auto found=complete.find(value.first);return found!=complete.end()&&found->second==value.second;}),"Allocation ancestor scope changed",ErrorCode::Conflict);work.directories=std::move(complete);
    for(auto& file:work.files){auto& slot=owner.slots.at(file.index);struct stat data{},parent{},named{};
      require(slot.data.get()>=0&&slot.parent.get()>=0&&::fstat(slot.data.get(),&data)==0&&::fstat(slot.parent.get(),&parent)==0
          &&::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
          &&sameExistingSnapshot(data,named)&&data.st_uid==::geteuid()&&data.st_nlink==1&&data.st_size>=0&&data.st_size<=file.bytes,
          "Allocation descriptor/name authority changed",ErrorCode::Conflict);
      if(slot.allocationCreated){file.created=true;file.removed=false;file.device=data.st_dev;file.inode=data.st_ino;file.parentDevice=parent.st_dev;file.parentInode=parent.st_ino;file.originalStamp=allocationStamp(data);}
      require(file.device==data.st_dev&&file.inode==data.st_ino&&file.parentDevice==parent.st_dev&&file.parentInode==parent.st_ino
          &&(file.created||file.originalStamp==allocationStamp(data)),"Allocation source was replaced",ErrorCode::Conflict);
      file.activeStamp=allocationStamp(data);file.prefix=data.st_size;file.written=data.st_size;file.scanned=0;file.authenticated=!file.created||file.prefix==0;
      if(!file.created){require(data.st_size==file.bytes,"Existing short data is outside allocation authority",ErrorCode::Unsupported);work.completed+=file.bytes;}
    }
    work.stage=1;auto attributes=dictionary(owner.state.document().find("attributes"));put(attributes,AllocationWorkKey,encodeAllocationWork(work));
    done(owner.state.replaceSection("attributes",attributes.serialize()));done(owner.store.save(lease,owner.state));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot durably begin allocation"});}
}
namespace {
template<class Owner> struct stat allocationCurrent(Owner& owner,const AllocationFile& file){
  auto& slot=owner.slots.at(file.index);struct stat data{},named{},parent{},actualParent{};
  checkAllocationChain(owner.allocation->directories,allocationChain(file.path.parent_path(),false));
  auto current=parentDirectory(file.path.parent_path(),false);
  require(current.get()>=0&&slot.parent.get()>=0&&::fstat(current.get(),&actualParent)==0&&::fstat(slot.parent.get(),&parent)==0
      &&actualParent.st_dev==parent.st_dev&&actualParent.st_ino==parent.st_ino
      &&file.parentDevice==parent.st_dev&&file.parentInode==parent.st_ino,"Allocation parent identity changed",ErrorCode::Conflict);
  require(slot.data.get()>=0&&::fstat(slot.data.get(),&data)==0&&::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
      &&sameExistingSnapshot(data,named)&&data.st_uid==::geteuid()&&data.st_nlink==1
      &&data.st_dev==file.device&&data.st_ino==file.inode&&data.st_size>=0&&data.st_size<=file.bytes,
      "Allocation file identity changed",ErrorCode::Conflict);
  return data;
}
}
std::expected<ManagedAllocationProgress,Error> ManagedStorage::advanceAllocation(std::size_t budget){
  try{
    require(impl_&&impl_->purpose==ManagedStoragePurpose::AllocateOnly&&impl_->allocation,"No writable allocation purpose",ErrorCode::Unsupported);
    auto& owner=*impl_;auto& work=*owner.allocation;require(!work.attempted,"Allocation finalization already attempted",ErrorCode::Conflict);
    require(budget>0&&budget<=64*1024*1024,"Allocation work quantum outside bounds",ErrorCode::InvalidFormat);
    std::array<char,65536> scratch{};
    while(budget&&work.cursor<work.files.size()){
      auto& file=work.files[work.cursor];auto& slot=owner.slots.at(file.index);const auto before=allocationCurrent(owner,file);
      require(allocationStamp(before)==file.activeStamp,"Allocation data changed between worker steps",ErrorCode::Conflict);
      if(!file.created){++work.cursor;continue;}
      if(file.scanned<file.prefix){
        const auto count=std::min({scratch.size(),budget,static_cast<std::size_t>(file.prefix-file.scanned)});ssize_t got;
        do{got=::pread(slot.data.get(),scratch.data(),count,file.scanned);}while(got<0&&errno==EINTR);
        require(got>0,"Cannot authenticate interrupted zero prefix",ErrorCode::Io);
        require(std::all_of(scratch.begin(),scratch.begin()+got,[](char value){return value==0;}),"Interrupted allocation contains unreviewed data",ErrorCode::Conflict);
        const auto after=allocationCurrent(owner,file);require(allocationStamp(after)==file.activeStamp,"Interrupted zero prefix changed while read",ErrorCode::Conflict);
        file.scanned+=got;if(file.scanned==file.prefix){file.authenticated=true;}if(file.prefix==file.bytes){file.verified=file.scanned;}work.completed+=got;budget-=static_cast<std::size_t>(got);continue;
      }
      if(file.written<file.bytes){
        const auto count=std::min({scratch.size(),budget,static_cast<std::size_t>(file.bytes-file.written)});std::fill(scratch.begin(),scratch.end(),0);ssize_t wrote;
        do{wrote=::pwrite(slot.data.get(),scratch.data(),count,file.written);}while(wrote<0&&errno==EINTR);
        require(wrote>0,"Actual zero allocation write failed",ErrorCode::Io);
        file.written+=wrote;work.completed+=wrote;budget-=static_cast<std::size_t>(wrote);
        const auto after=allocationCurrent(owner,file);require(after.st_size==file.written,"Allocation extent changed outside its write",ErrorCode::Conflict);file.activeStamp=allocationStamp(after);
      }
      if(file.written==file.bytes){
        // Our writes change ctime. Authenticate the complete newly-created zero
        // extent afterwards; an external edit cannot be hidden by refreshing a
        // stamp following one of our later writes.
        if(file.verified<file.bytes&&budget){
          const auto count=std::min({scratch.size(),budget,static_cast<std::size_t>(file.bytes-file.verified)});ssize_t got;
          do{got=::pread(slot.data.get(),scratch.data(),count,file.verified);}while(got<0&&errno==EINTR);
          require(got>0,"Cannot authenticate completed allocation",ErrorCode::Io);
          require(std::all_of(scratch.begin(),scratch.begin()+got,[](char c){return c==0;}),"Allocated extent changed outside zero writes",ErrorCode::Conflict);
          const auto after=allocationCurrent(owner,file);require(allocationStamp(after)==file.activeStamp,"Allocated extent changed during final read",ErrorCode::Conflict);
          file.verified+=got;budget-=static_cast<std::size_t>(got);
        }
        if(file.verified==file.bytes){sync(slot.data.get());const auto after=allocationCurrent(owner,file);require(allocationStamp(after)==file.activeStamp,"Allocated file changed before durable completion",ErrorCode::Conflict);++work.cursor;}
      }
    }
    return allocationProgress();
  }catch(const Failure& error){if(impl_&&impl_->allocation&&error.error.code==ErrorCode::Conflict){impl_->allocation->integrityLost=true;}return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Actual zero allocation did not finish"});}
}
std::expected<void,Error> ManagedStorage::settleAllocation(const storage::ProfileLease& lease,bool successful){
  try{
    require(impl_&&impl_->purpose==ManagedStoragePurpose::AllocateOnly&&impl_->allocation,"No writable allocation purpose",ErrorCode::Unsupported);
    auto& owner=*impl_;auto& work=*owner.allocation;require(!work.attempted,"Allocation finalization already attempted",ErrorCode::Conflict);
    require(!successful||(work.cursor==work.files.size()&&work.completed==work.total),"Unfinished allocation cannot publish success",ErrorCode::Conflict);
    allocationStateCurrent(owner,lease);
    const auto fresh=take(storage::readAllocationIntent(lease,owner.hash));
    require(fresh&&fresh->row==work.intent.row&&fresh->paused==work.intent.paused,"Allocation intent changed before settlement",ErrorCode::Conflict);
    require(!work.integrityLost,"Allocation integrity was lost; unreviewed data is retained",ErrorCode::Conflict);
    // Preflight the whole immutable scope before the first cancellation unlink.
    for(const auto& file:work.files){require(successful||!file.created||file.authenticated,"Interrupted allocation prefix needs authentication before cancellation",ErrorCode::Conflict);const auto current=allocationCurrent(owner,file);require(allocationStamp(current)==file.activeStamp,"Allocation scope changed before settlement",ErrorCode::Conflict);}
    work.attempted=true; // No replay after any publication/durability uncertainty.
    for(auto& file:work.files){auto& slot=owner.slots.at(file.index);sync(slot.data.get());
      if(!successful&&file.created&&file.written<file.bytes){
        const auto current=allocationCurrent(owner,file);require(allocationStamp(current)==file.activeStamp,"Cancellation target changed",ErrorCode::Conflict);
        require(::unlinkat(slot.parent.get(),slot.actual.filename().c_str(),0)==0,"Cannot remove exact incomplete new allocation",ErrorCode::Io);sync(slot.parent.get());file.removed=true;
      }
    }
    work.stage=successful?2:3;
    auto attributes=dictionary(owner.state.document().find("attributes"));put(attributes,AllocationWorkKey,encodeAllocationWork(work));
    done(owner.state.replaceSection("attributes",attributes.serialize()));
    // Allocated zeroes are never piece verification. Existing lifetime counters
    // and unknown metadata stay unchanged, including on cancellation/failure.
    writeProgress(owner.state,std::vector<std::int64_t>(owner.layout.files.size(),0),static_cast<std::size_t>(owner.params.ti->num_pieces()),false);
    done(owner.store.save(lease,owner.state));
    done(storage::publishAllocationState(lease,work.intent,successful?storage::AllocationState::Complete:storage::AllocationState::Cancelled));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Allocation settlement failed; inspect recovery before retry"});}
}
