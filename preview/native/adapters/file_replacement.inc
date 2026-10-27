// Explicit one-file replacement only. Prior destination is never discarded.
void checkReplacementBeforeJournal(Plan& p,std::stop_token stop){
  const auto& f=p.files.front();require(p.replacement&&f.destination&&f.source,ErrorCode::InvalidFormat,"Replacement authority is incomplete");
  require(!directoryExists(p.replacement->root),ErrorCode::Conflict,"Reserved preimage path appeared after review");
  const auto now=inspect(f.view.destination);require(now&&unchanged(*now,*f.destination)&&p.payloadDigests.digest(f.view.destination,*now,stop)==p.replacement->sha256,ErrorCode::Conflict,"Reviewed prior destination changed");
}
void preimageDirectoryIdentity(const Plan& p){
  const auto& r=*p.replacement;require(r.directoryInode,ErrorCode::Conflict,"Preimage directory was not sealed");auto fd=directory(r.root);struct stat s{};
  require(::fstat(fd.get(),&s)==0&&s.st_uid==::geteuid()&&(s.st_mode&0077)==0&&s.st_dev==r.directoryDevice&&s.st_ino==r.directoryInode,ErrorCode::Conflict,"Retained preimage directory changed");
}
void validatePreimage(Plan& p){
  preimageDirectoryIdentity(p);const auto& f=p.files.front();const auto path=p.replacement->root/"data";const auto now=inspect(path);
  require(now&&f.destination&&identity(*now,*f.destination)&&now->st_nlink==1&&now->st_size==f.destination->st_size&&p.payloadDigests.digest(path,*now)==p.replacement->sha256,ErrorCode::Conflict,"Retained destination preimage changed; source is not discarded");
}
void preserveDestination(Plan& p,void(*observer)(FileOperationStage)){
  auto& r=*p.replacement;const auto& f=p.files.front();targetIdentity(p);
  if(!r.directoryInode){
    // Do not adopt even an apparently empty directory created by somebody else.
    auto parent=directory(r.root.parent_path());require(::mkdirat(parent.get(),r.root.filename().c_str(),0700)==0,ErrorCode::Conflict,"Cannot exclusively create reviewed preimage directory");sync(parent.get());
    auto fd=directory(r.root);struct stat created{};require(::fstat(fd.get(),&created)==0&&created.st_uid==::geteuid()&&(created.st_mode&0077)==0,ErrorCode::UnsafePath,"Unsafe preimage directory");
    r.directoryDevice=created.st_dev;r.directoryInode=created.st_ino;saveJournal(p);
    if(observer) observer(FileOperationStage::PreimageDirectorySealed);
  }
  preimageDirectoryIdentity(p);const auto path=r.root/"data";
  const auto source=inspect(f.view.source);const auto destination=inspect(f.view.destination);
  if(source){require(identity(*source,*f.source)&&p.payloadDigests.digest(f.view.source,*source)==f.sha256,ErrorCode::Conflict,"Replacement source path changed during recovery");}
  else{require(destination&&(identity(*destination,*f.source)||(f.temporary&&identity(*destination,*f.temporary)))&&p.payloadDigests.digest(f.view.destination,*destination)==f.sha256,ErrorCode::Conflict,"Replacement source and its published destination are missing");}
  if(inspect(path)){validatePreimage(p);return;}
  const auto prior=inspect(f.view.destination);require(prior&&f.destination&&unchanged(*prior,*f.destination)&&p.payloadDigests.digest(f.view.destination,*prior)==r.sha256,ErrorCode::Conflict,"Prior destination is not the reviewed preimage");
  auto from=directory(f.view.destination.parent_path());auto to=directory(r.root);
  require(::syscall(SYS_renameat2,from.get(),f.view.destination.filename().c_str(),to.get(),"data",RENAME_NOREPLACE)==0,ErrorCode::Conflict,"Cannot preserve exact prior destination without overwrite");sync(to.get());sync(from.get());
  validatePreimage(p);if(observer) observer(FileOperationStage::PreimagePreserved);
}
