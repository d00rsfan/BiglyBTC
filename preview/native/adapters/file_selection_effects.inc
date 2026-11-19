// The complete current census is retained by the ordinary FileOperation owner.
// Pure Source decisions never constitute pathname or creation authority.
constexpr std::string_view SelectionAbsentKey="BiglyBTC.selectionAbsent";
bool effectMoves(const Payload& f){return f.view.source!=f.view.destination&&!f.selectionRemove;}
std::pair<fs::path,struct stat> selectionAnchor(const fs::path& path){
  auto parent=path.parent_path();auto fd=directory(parent,false,true);
  while(fd.get()<0){require(parent!=parent.parent_path(),ErrorCode::Missing,"Selection allocation has no available ancestor");parent=parent.parent_path();fd=directory(parent,false,true);}
  struct stat s{};require(::fstat(fd.get(),&s)==0&&s.st_uid==::geteuid(),ErrorCode::UnsafePath,"Selection ancestor is not owned");return {parent,s};
}
void checkSelectionAnchor(const Payload& f){
  auto fd=directory(f.selectionAnchor);struct stat s{};
  require(fd.get()>=0&&::fstat(fd.get(),&s)==0&&s.st_uid==::geteuid()&&static_cast<std::uint64_t>(s.st_dev)==f.selectionDevice&&static_cast<std::uint64_t>(s.st_ino)==f.selectionInode,ErrorCode::Conflict,"Selection storage ancestor changed; reconnect its original volume");
}
bool absentSelectionProof(const Node& attrs,std::size_t index,const fs::path& path){
  const auto* all=attrs.find(SelectionAbsentKey);if(!all){return false;}
  require(all->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid selection absence receipts");
  const auto* proof=all->find(std::to_string(index));if(!proof){return false;}
  require(proof->type==Node::Type::Dictionary&&proof->children.size()==8,ErrorCode::InvalidFormat,"Invalid selection absence receipt");
  require(text(proof->find("path"))==path.string(),ErrorCode::Conflict,"Selection absence path changed");
  Payload f;f.selectionAnchor=text(proof->find("anchor"));pathShape(f.selectionAnchor);require(within(path,f.selectionAnchor),ErrorCode::InvalidFormat,"Selection absence ancestor differs");
  f.selectionDevice=unsignedText(proof->find("device"));f.selectionInode=unsignedText(proof->find("inode"));checkSelectionAnchor(f);return true;
}
std::vector<std::pair<bool,bool>> selectionDecisions(const Plan& p,const compat::OriginalLayout& layout,
    const Node& state,const Node& afterAttrs,std::span<const compat::FilePolicy> before,
    std::span<const compat::FilePolicy> after,const Node* explicitMask){
  std::vector<compat::OriginalFilePieceSpan> spans;std::vector<std::int32_t> kinds;
  std::vector<std::uint8_t> exists(layout.files.size(),1),skip(layout.files.size()),unskip(layout.files.size());
  for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];auto span=compat::originalFilePieceSpan(file.offset,file.bytes,layout.pieceLength,after[n].skipped);require(span.has_value(),ErrorCode::InvalidFormat,"Selection piece geometry invalid");spans.push_back(*span);kinds.push_back(static_cast<std::int32_t>(storageKind(afterAttrs,n,layout.files.size())));
    skip[n]=!before[n].skipped&&after[n].skipped;unskip[n]=before[n].skipped&&!after[n].skipped;}
  if(explicitMask){std::fill(skip.begin(),skip.end(),0);std::fill(unskip.begin(),unskip.end(),0);auto chosen=compat::readOriginalFileSelectionIntent(*explicitMask,layout.files.size());require(chosen.has_value(),ErrorCode::InvalidFormat,"Invalid selection effects mask");for(std::size_t n=0;n<layout.files.size();++n){if(chosen->changed[n]){(chosen->skipped?skip:unskip)[n]=1;}}}
  for(const auto& file:p.files){exists[file.view.index]=file.source.has_value();}
  std::optional<std::span<const std::uint8_t>> resume;
  if(const auto* outer=state.find("resume")){require(outer->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid selection resume envelope");if(const auto* data=outer->find("data")){require(data->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid selection resume data");if(const auto* flags=data->find("resume data")){require(flags->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid selection piece flags");resume=std::span(reinterpret_cast<const std::uint8_t*>(flags->bytes.data()),flags->bytes.size());}}}
  const auto skipped=compat::originalPostSkipExistence(spans,kinds,skip,exists,true,true,resume);
  const auto selected=compat::originalPostSkipExistence(spans,kinds,unskip,exists,false,true,resume);
  require(skipped&&selected,ErrorCode::InvalidFormat,"Selection existence facts are incomplete");
  std::vector<std::pair<bool,bool>> result(layout.files.size());for(std::size_t n=0;n<result.size();++n){result[n]={bool(skipped->compactRemovals[n]),bool(selected->allocationRequests[n])};}
  // The later Original allocator requires every noncompact file,including a
  // zero-length file whose reversed piece span is not required by the earlier
  // RD predicate. Materialize this explicitly selected empty leaf here;the
  // usual previous-allocation/absence proof still precedes creation.
  for(const auto& f:p.files){const auto index=f.view.index;if(!f.source&&layout.files[index].bytes==0&&unskip[index]&&(kinds[index]==1||kinds[index]==3)){result[index].second=true;}}return result;
}
void applySelectionReceipts(const Plan& p,core::Document& attrs){
  auto receipts=section(attrs.find(SelectionAbsentKey));bool changed=false;
  for(const auto& f:p.files){const auto key=std::to_string(f.view.index);
    if((f.selectionAllocate||f.source)&&receipts.find(key)){require(receipts.eraseValue(key).has_value(),ErrorCode::InvalidFormat,"Cannot consume selection absence receipt");changed=true;}
    if(f.selectionRemove){auto proof=parse("de");put(proof,"path",b(f.view.destination.string()));put(proof,"anchor",b(f.selectionAnchor.string()));put(proof,"device",b(std::to_string(f.selectionDevice)));put(proof,"inode",b(std::to_string(f.selectionInode)));put(receipts,key,proof.serialize());changed=true;}
  }
  if(changed){put(attrs,SelectionAbsentKey,receipts.serialize());}
}
fs::path selectionRecoveryDestination(const Plan& p,const Payload& f,const core::SavedDownload& saved,
    const compat::OriginalLayout& layout,const Node& attrs,std::span<const compat::FilePolicy> before,std::span<const compat::FilePolicy> after){
  const auto target=selectionDestination(saved,layout,attrs,f.view.index,f.view.source,before[f.view.index],after[f.view.index]);
  if(p.postSkip&&f.view.destination==f.view.source&&target!=f.view.source){require(inspect(target).has_value(),ErrorCode::Conflict,"Previously occupied optional DND destination disappeared");return f.view.source;}return target;
}
void prepareSelectionEffects(Plan& p,const compat::OriginalLayout& layout,const Node& state,core::Document& attrs,
    const Node& row,std::span<const compat::FilePolicy> before,std::span<const compat::FilePolicy> after,const Node* mask){
  const auto decisions=selectionDecisions(p,layout,state,attrs.root(),before,after,mask);p.postSkip=true;
  for(auto& file:p.files){const auto [remove,allocate]=decisions[file.view.index];file.selectionRemove=remove;file.selectionAllocate=allocate;
    if(!remove&&!allocate){continue;}
    if(allocate){const auto* allocated=row.find("allocated");if(allocated&&static_cast<std::uint32_t>(integer(allocated))==1){require(absentSelectionProof(attrs.root(),file.view.index,file.view.source),ErrorCode::Missing,"Previously allocated data is missing; reconnect its storage before selecting it");}
    }
    const auto [anchor,s]=selectionAnchor(file.view.destination);file.selectionAnchor=anchor;file.selectionDevice=s.st_dev;file.selectionInode=s.st_ino;

  }
  applySelectionReceipts(p,attrs);
}
bool selectionRetiresControl(const Plan& p,const fs::path& path){
  if(!p.postSkip){return false;}for(const auto& f:p.files){if(!f.selectionRemove){continue;}for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){const auto leaf=originalControlLeaf(f.view.index,kind);if(path.filename()==leaf||path.filename()==leaf+".bak"){return true;}}}return false;
}
constexpr const char* AllocationReceipt="user.BiglyBTC.allocationReceipt";
void checkAllocationReceipt(const fs::path& path,const Payload& f,const struct stat& current){
  require(f.allocationToken.size()==64,ErrorCode::Conflict,"Interrupted empty-file allocation needs review on this filesystem");
  auto parent=directory(path.parent_path());Fd held(::openat(parent.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat opened{};std::array<char,64> token{};
  require(held.get()>=0&&::fstat(held.get(),&opened)==0&&identity(opened,current)&&opened.st_nlink==1&&opened.st_size==0&&::fgetxattr(held.get(),AllocationReceipt,token.data(),token.size())==static_cast<ssize_t>(token.size())&&std::string_view(token.data(),token.size())==f.allocationToken,ErrorCode::Conflict,"Interrupted allocation target does not carry its creation receipt");
}
void writeAllocationReceipt(int fd,Payload& f){
  std::array<unsigned char,32> bytes{};require(RAND_bytes(bytes.data(),bytes.size())==1,ErrorCode::Io,"Cannot generate allocation creation receipt");constexpr char digits[]="0123456789abcdef";for(const auto c:bytes){f.allocationToken+=digits[c>>4];f.allocationToken+=digits[c&15];}
  if(::fsetxattr(fd,AllocationReceipt,f.allocationToken.data(),f.allocationToken.size(),XATTR_CREATE)!=0){require(errno==ENOTSUP||errno==EOPNOTSUPP||errno==ENOSYS,ErrorCode::Io,"Cannot bind allocation creation receipt");f.allocationToken.clear();}
}
void executeSelectionEffects(Plan& p,const auto& notify){
  if(!p.postSkip){return;}
  for(auto& f:p.files){if(!f.selectionAllocate){continue;}checkSelectionAnchor(f);auto parent=directory(f.view.destination.parent_path(),true);struct stat s{};auto target=inspect(f.view.destination);const auto scratch=temporaryPath(p,f);
    if(target){require(f.temporary&&identity(*target,*f.temporary)&&target->st_nlink==1&&target->st_size==0,ErrorCode::Conflict,"An unrelated file occupies the selection allocation target");checkAllocationReceipt(f.view.destination,f,*target);}
    else{
      auto prior=inspect(scratch);Fd fd(-1);
      if(prior){require(f.temporary&&identity(*prior,*f.temporary)&&prior->st_nlink==1&&prior->st_size==0,ErrorCode::Conflict,"Unrecorded selection scratch requires review");checkAllocationReceipt(scratch,f,*prior);}
      else{require(!f.temporary,ErrorCode::Conflict,"Owned selection allocation disappeared");fd=Fd(::openat(parent.get(),scratch.filename().c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(fd.get()>=0&&::fstat(fd.get(),&s)==0&&s.st_nlink==1&&s.st_uid==::geteuid(),ErrorCode::Io,"Cannot create exclusive selection allocation");writeAllocationReceipt(fd.get(),f);sync(fd.get());require(::fstat(fd.get(),&s)==0,ErrorCode::Io,"Cannot capture allocation receipt identity");f.temporary=s;sync(parent.get());saveJournal(p);notify(FileOperationStage::AllocationPrepared);}
      require(::syscall(SYS_renameat2,parent.get(),scratch.filename().c_str(),parent.get(),f.view.destination.filename().c_str(),RENAME_NOREPLACE)==0,ErrorCode::Conflict,"Selection allocation target appeared");sync(parent.get());
    }
    notify(FileOperationStage::AllocationPublished);
  }
  for(const auto& f:p.files){if(!f.selectionRemove){continue;}checkSelectionAnchor(f);const auto now=inspect(f.view.source);
    if(now){require(f.source&&identity(*now,*f.source)&&p.payloadDigests.digest(f.view.source,*now)==f.sha256,ErrorCode::Conflict,"Unneeded compact payload changed before removal");erase(f.view.source,*now);}notify(FileOperationStage::CompactRemoved);
    for(const auto& c:p.controls){if(!selectionRetiresControl(p,c.path)){continue;}const auto current=read(c.path,MaximumDocument);if(current.bytes){check(c.path,c.before,MaximumDocument);erase(c.path,*current.stamp);}}
  }
}
