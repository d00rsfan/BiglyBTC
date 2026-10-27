// Private Linux home-trash helpers, included inside file_operations.cpp.
// No IO effects during preparation. All effects follow the confirmed journal.
void validateTrashDate(std::string_view date){
  require(date.size()==19,ErrorCode::InvalidFormat,"Invalid trash deletion date");
  for(std::size_t i=0;i<date.size();++i){const auto punctuation=i==4||i==7?'-':i==10?'T':i==13||i==16?':':'\0';require(punctuation?date[i]==punctuation:date[i]>='0'&&date[i]<='9',ErrorCode::InvalidFormat,"Invalid trash deletion date");}
}
void privateTrashDirectory(const fs::path& path,bool create){
  auto fd=directory(path,create,true);if(fd.get()<0){return;}struct stat s{};
  require(::fstat(fd.get(),&s)==0&&s.st_uid==::geteuid()&&(s.st_mode&0077)==0,ErrorCode::UnsafePath,"Trash directory must be private and owned");
}
void prepareTrash(Plan& p,const fs::path& root){
  payloadPath(root,p.profile);require(root.filename()=="Trash"&&!intersects(root,p.profile)&&!intersects(root,p.preview.sourceRoot),ErrorCode::UnsafePath,"Trash must be a separate owned home-trash directory");
  // Anchor the first existing parent; newly created components remain below it.
  auto anchor=root;for(;;){auto fd=directory(anchor,false,true);if(fd.get()>=0){struct stat s{};require(::fstat(fd.get(),&s)==0&&s.st_uid==::geteuid(),ErrorCode::UnsafePath,"Trash ancestor must be owned");p.destinationParent=anchor;p.targetDevice=s.st_dev;p.targetInode=s.st_ino;break;}
    const auto parent=anchor.parent_path();require(parent!=anchor&&!parent.empty(),ErrorCode::UnsafePath,"No owned trash ancestor");anchor=parent;}
  privateTrashDirectory(root,false);privateTrashDirectory(root/"files",false);privateTrashDirectory(root/"info",false);
  const std::time_t now=std::time(nullptr);std::tm local{};std::array<char,32> date{};
  require(now!=std::time_t{-1}&&::localtime_r(&now,&local)&&std::strftime(date.data(),date.size(),"%Y-%m-%dT%H:%M:%S",&local)==19,ErrorCode::Io,"Cannot record trash deletion time");
  p.trash=Trash{root,date.data()};p.preview.targetRoot=root/"files";
}
std::string trashPrefix(const Plan& p,const Payload& f){return p.preview.hash.hex()+'.'+std::to_string(f.view.index)+'.';}
bool trashLeafFor(const Plan& p,const Payload& f){
  const auto name=f.view.destination.filename().string(),prefix=trashPrefix(p,f);if(!name.starts_with(prefix)){return false;}
  const auto counter=name.substr(prefix.size());unsigned value=0;const auto parsed=std::from_chars(counter.data(),counter.data()+counter.size(),value);
  return !counter.empty()&&parsed.ec==std::errc{}&&parsed.ptr==counter.data()+counter.size()&&value<1024&&std::to_string(value)==counter;
}
fs::path trashDestination(const Plan& p,const Payload& f){
  for(unsigned n=0;n<1024;++n){const auto name=trashPrefix(p,f)+std::to_string(n);const auto data=p.trash->root/"files"/name,info=p.trash->root/"info"/(name+".trashinfo");
    if(!inspect(data)&&!inspect(info)&&!inspect(fs::path(info.string()+".BiglyBTC.fileop-saving"))){return data;}}
  fail(ErrorCode::Conflict,"Trash name bound exhausted; existing entries are preserved");
}
std::string trashInfo(const Plan& p,const Payload& f){
  static constexpr char hex[]="0123456789ABCDEF";std::string path;
  for(const unsigned char c:f.view.source.string()){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_'||c=='.'||c=='~'){path+=static_cast<char>(c);}else{path+='%';path+=hex[c>>4];path+=hex[c&15];}}
  return "[Trash Info]\nPath="+path+"\nDeletionDate="+p.trash->deletionDate+"\n";
}
void ensureTrash(Plan& p){
  bool newlySealed=!p.trash->directories[0].second;
  const std::array<fs::path,3> paths{p.trash->root,p.trash->root/"files",p.trash->root/"info"};
  for(std::size_t n=0;n<paths.size();++n){privateTrashDirectory(paths[n],true);auto fd=directory(paths[n]);struct stat id{};require(::fstat(fd.get(),&id)==0,ErrorCode::Io,"Cannot identify trash directory");
    const auto identity=std::pair<std::uint64_t,std::uint64_t>{id.st_dev,id.st_ino};
    if(newlySealed){p.trash->directories[n]=identity;}else{require(p.trash->directories[n]==identity,ErrorCode::Conflict,"Trash directory changed after publication");}}
  // Bind the actual created directories durably BEFORE any payload publication.
  if(newlySealed){saveJournal(p);}
}
void publishTrashInfo(const Plan& p){
  for(const auto& f:p.files){if(!f.source){continue;}require(trashLeafFor(p,f),ErrorCode::InvalidFormat,"Invalid trash leaf identity");
    const auto path=p.trash->root/"info"/(f.view.destination.filename().string()+".trashinfo");const auto bytes=trashInfo(p,f);const auto current=read(path,16384);
    if(current.bytes){require(*current.bytes==bytes,ErrorCode::Conflict,"Trash information changed; source retained");}
    else{publish(path,current,bytes,16384);}
  }
}
