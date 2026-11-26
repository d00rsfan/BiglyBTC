// Included only in intake's private implementation after Plan is complete.
// This one derivative changes the actual newly saved stopped row to Queued75.
// It is never an arbitrary inventory postimage supplied by a caller.
std::string automaticAdmissionInventory(const Plan& plan,bool queued,bool paused){
  require(!(queued&&paused),ErrorCode::Conflict,"Automatic outcome cannot be queued and paused");if(!queued&&!paused){return plan.inventory;}
  auto source=parse(plan.inventory,MaximumInventory);
  const auto library=core::readLibrary(source);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Automatic queue inventory is malformed");
  const core::SavedDownload* selected=nullptr;
  for(const auto& row:library->downloads){
    if(row.hash&&*row.hash==plan.hash){require(!selected,ErrorCode::Conflict,"Automatic queue target is not unique");selected=&row;}
  }
  require(selected&&selected->persistent&&selected->startup==core::StartupState::Stopped
      &&selected->originalState==70&&!selected->forceStart,ErrorCode::Conflict,"Automatic queue needs its genuine new stopped row");
  for(const auto& paused:library->paused){require(paused.hash!=plan.hash,ErrorCode::Conflict,"Automatic queue cannot clear a saved pause");}
  const auto* rows=source.find("downloads");
  const Node& row=library->legacyMap?source.root().children.at(selected->sourceIndex*2+1):rows->children.at(selected->sourceIndex);
  auto changed=parse(raw(source,row));put(changed,"state",i(queued?75:70));if(changed.find("stopped")){put(changed,"stopped",i(queued?0:1));}
  if(library->legacyMap){put(source,source.root().children.at(selected->sourceIndex*2).bytes,changed.serialize());}
  else{
    require(rows&&rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Automatic queue rows missing");
    std::string list="l";for(std::size_t n=0;n<rows->children.size();++n){list+=n==selected->sourceIndex?changed.serialize():raw(source,rows->children[n]);}
    put(source,"downloads",list+'e');
  }
  if(paused){
    const auto* previous=source.find("pause_data");require(!previous||previous->type==Node::Type::List,ErrorCode::InvalidFormat,"Automatic pause list is malformed");std::string list=previous?raw(source,*previous):"le";
    const auto bytes=plan.hash.bytes();list.insert(list.size()-1,"d4:hash"+b(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()))+"5:forcei0e5:owneri1ee");put(source,"pause_data",std::move(list));
  }
  const auto encoded=source.serialize();const auto checked=core::readLibrary(parse(encoded,MaximumInventory));
  require(checked&&checked->issues.empty(),ErrorCode::InvalidFormat,"Cannot recheck automatic queued inventory");
  const auto found=std::ranges::find_if(checked->downloads,[&](const auto& value){return value.hash&&*value.hash==plan.hash;});
  require(found!=checked->downloads.end()&&found->startup==(queued?core::StartupState::Queued:core::StartupState::Stopped)&&found->originalState==(queued?75:70)
      &&!found->forceStart,ErrorCode::Conflict,"Automatic queued inventory is not the fixed derivative");
  const auto pause=std::ranges::find_if(checked->paused,[&](const auto& value){return value.hash==plan.hash;});require((pause!=checked->paused.end())==paused&&checked->paused.size()==library->paused.size()+(paused?1U:0U)&&(!paused||(!pause->forceStart&&pause->owner==1)),ErrorCode::Conflict,"Automatic pause is not the fixed general-pause derivative");
  return encoded;
}

// Exact directory scopes omit mtime/ctime: creating the owned metainfo file may
// legitimately change its parent, while inode/owner/mode/group must stay bound.
bool automaticScopeEqual(const StateStamp& now,const StateStamp& before){
  return detail::sameStateDirectory(now, before);
}
StateStamp automaticDirectoryStamp(const fs::path& path){
  auto fd=directory(path);const auto info=take(stateStamp(fd.get()));
  require(info.directory&&info.owned,ErrorCode::UnsafePath,"Automatic directory is not actually owned");return info;
}
std::optional<StateStamp> automaticProbe(const fs::path& path){auto parent=directory(path.parent_path());return named(parent.get(),path.filename());}
search::detail::AutomaticDataLocationKind automaticLocationKind(const std::optional<StateStamp>& info){
  using Kind=search::detail::AutomaticDataLocationKind;
  if(!info){return Kind::Absent;}
  require(info->owned,ErrorCode::UnsafePath,"Automatic save candidate is not owned");
  if(info->directory){return Kind::Directory;}
  if(info->regular&&info->links==1){return Kind::RegularFile;}
  fail(ErrorCode::UnsafePath,"Automatic save candidate is symbolic, shared or special");
}
std::string existingDictionary(const std::map<std::string,std::string>&);
std::string existingStampBytes(const StateStamp&);
StateStamp existingStampValue(const Node&);
std::string automaticFileValue(const std::optional<File>& file){return file?existingDictionary({{"bytes",b(file->bytes)},{"stamp",existingStampBytes(file->info)}}):i(0);}
std::optional<File> automaticFileFrom(const Node* node){
  require(node,ErrorCode::InvalidFormat,"Automatic file observation missing");
  if(node->type==Node::Type::Integer){require(node->integer==0,ErrorCode::InvalidFormat,"Automatic absence invalid");return {};}
  require(node->type==Node::Type::Dictionary&&node->children.size()==4,ErrorCode::InvalidFormat,"Automatic file observation malformed");
  const auto* bytes=node->find("bytes"),*stamp=node->find("stamp");require(bytes&&bytes->type==Node::Type::Bytes&&bytes->bytes.size()<=MaximumMetadata&&stamp,ErrorCode::InvalidFormat,"Automatic file observation exceeds bound");
  auto info=existingStampValue(*stamp);regular(info);require(info.size>=0&&static_cast<std::uint64_t>(info.size)==bytes->bytes.size(),ErrorCode::InvalidFormat,"Automatic file observation size differs");return File{std::string(bytes->bytes),info};
}
struct AutomaticIntakeContext {
  search::detail::SubscriptionAutomaticPolicyValues policy;
  search::detail::SubscriptionAutomaticGeometry geometry;
  fs::path profile,probePath,copyPath;
  File settings;std::optional<File> settingsBackup,copyBefore,created,inventoryBackup;bool queueBound=false;
  StateStamp defaultScope{};std::optional<StateStamp> copyParentScope,probeBefore;
  std::string body;
  static AutomaticIntakeContext capture(const ProfileLease& lease,const MetadataSubject& metadata,std::string actualArchive,std::string_view filenameConversions){
    AutomaticIntakeContext out;out.profile=pathFromUtf8(profilePath(lease));const auto root=lease.directoryHandle();
    absent(root,"biglybt.config.saving");absent(root,"biglybt.config.saving.bak");
    auto config=read(root,"biglybt.config",MaximumMetadata);require(config.has_value(),ErrorCode::Unsupported,"Automatic acquisition needs actual primary settings");out.settings=std::move(*config);out.settingsBackup=read(root,"biglybt.config.bak",MaximumMetadata);
    const auto values=search::detail::readSubscriptionAutomaticPolicy(parse(out.settings.bytes,MaximumMetadata),pathToUtf8(out.profile));require(values.has_value(),ErrorCode::Unsupported,"Automatic saved policy or save location is unsupported");out.policy=*values;
    const auto saveRoot = pathFromUtf8(out.policy.defaultSaveDirectory);pathShape(saveRoot);out.defaultScope=automaticDirectoryStamp(saveRoot);
    require(metadata.identity.selected==OriginalSelectedHash::V1&&metadata.identity.full.v1&&!metadata.identity.full.v2,ErrorCode::Unsupported,"Automatic intake needs actual pure v1");
    std::array<std::byte,20> hash{};std::copy(metadata.hash.bytes().begin(),metadata.hash.bytes().end(),hash.begin());
    auto initial=search::detail::subscriptionAutomaticGeometry(out.policy,metadata.layout.name,hash,metadata.layout.multifile,metadata.layout.bytes,filenameConversions,out.policy.addHashNames?search::detail::AutomaticDataLocationKind::Absent:search::detail::AutomaticDataLocationKind::Directory);
    require(initial.has_value(),ErrorCode::Unsupported,"Automatic saved geometry is unsupported");
    out.probePath=out.policy.addHashNames?pathFromUtf8(metadata.layout.multifile?initial->destinationDirectory:initial->dataLocation):saveRoot;
    out.probeBefore=automaticProbe(out.probePath);
    const auto geometry=search::detail::subscriptionAutomaticGeometry(out.policy,metadata.layout.name,hash,metadata.layout.multifile,metadata.layout.bytes,filenameConversions,automaticLocationKind(out.probeBefore));require(geometry.has_value(),ErrorCode::Unsupported,"Automatic actual save geometry is unsupported");out.geometry=*geometry;
    destinationPath(pathFromUtf8(out.geometry.destinationDirectory));out.copyPath=pathFromUtf8(out.geometry.torrentFile);pathShape(out.copyPath);out.body=std::move(actualArchive);require(out.copyPath!=out.profile/"torrents"/(metadata.hash.hex()+".torrent"),ErrorCode::Unsupported,"Configured named torrent collides with the fixed canonical archive");
    const auto parent=out.copyPath.parent_path();
    // The normal initial intake may create its own fixed control archive dir.
    // Arbitrary missing external directories are never created by this plan.
    if(parent==out.profile/"torrents"){
      const auto state=named(root,"torrents");if(state){out.copyParentScope=automaticDirectoryStamp(parent);}
    }else{out.copyParentScope=automaticDirectoryStamp(parent);}
    if(out.copyParentScope){auto fd=directory(parent);absent(fd.get(),withSuffix(out.copyPath.filename(), ".BiglyBTC.auto-saving"));out.copyBefore=read(fd.get(),out.copyPath.filename(),MaximumMetadata);}
    if(out.copyBefore){require(out.copyBefore->bytes==out.body,ErrorCode::Conflict,"Configured torrent file belongs to different existing bytes");}
    return out;
  }
  std::string encoded()const{
    return existingDictionary({{"schema",b("BiglyBTC.AutomaticIntakeContext.V1")},{"profile",b(pathToUtf8(profile))},{"settings",automaticFileValue(settings)},{"settingsBackup",automaticFileValue(settingsBackup)},{"defaultScope",existingStampBytes(defaultScope)},{"copyParentScope",copyParentScope?existingStampBytes(*copyParentScope):i(0)},{"probeBefore",probeBefore?existingStampBytes(*probeBefore):i(0)},{"copyBefore",automaticFileValue(copyBefore)},{"created",automaticFileValue(created)},{"queueBound",i(queueBound?1:0)},{"inventoryBackup",automaticFileValue(inventoryBackup)}});
  }
  static AutomaticIntakeContext restore(const ProfileLease& lease,const MetadataSubject& metadata,
      std::string actualArchive,std::string_view filenameConversions,const std::string& bytes){
    const auto value=parse(bytes,MaximumJournal);uniqueKeys(value.root());
    require(value.root().children.size()==22&&text(value.find("schema"))=="BiglyBTC.AutomaticIntakeContext.V1",ErrorCode::InvalidFormat,"Unknown automatic intake context");
    AutomaticIntakeContext out;out.profile=pathFromUtf8(profilePath(lease));require(text(value.find("profile"))==pathToUtf8(out.profile),ErrorCode::Conflict,"Automatic context belongs to another profile");
    auto settings=automaticFileFrom(value.find("settings"));require(settings.has_value(),ErrorCode::InvalidFormat,"Automatic actual settings witness missing");out.settings=std::move(*settings);out.settingsBackup=automaticFileFrom(value.find("settingsBackup"));out.copyBefore=automaticFileFrom(value.find("copyBefore"));out.created=automaticFileFrom(value.find("created"));out.inventoryBackup=automaticFileFrom(value.find("inventoryBackup"));const auto bound=integer(value.find("queueBound"));require(bound==0||bound==1,ErrorCode::InvalidFormat,"Invalid automatic queue witness");out.queueBound=bound==1;require(out.queueBound||!out.inventoryBackup,ErrorCode::InvalidFormat,"Unexpected automatic queue backup witness");
    const auto values=search::detail::readSubscriptionAutomaticPolicy(parse(out.settings.bytes,MaximumMetadata),pathToUtf8(out.profile));require(values.has_value(),ErrorCode::InvalidFormat,"Frozen automatic policy is unsupported");out.policy=*values;
    const auto stamp=[&](std::string_view key)->std::optional<StateStamp>{const auto* node=value.find(key);require(node,ErrorCode::InvalidFormat,"Automatic scope witness missing");if(node->type==Node::Type::Integer){require(node->integer==0,ErrorCode::InvalidFormat,"Automatic missing scope malformed");return {};}return existingStampValue(*node);};
    auto rootScope=stamp("defaultScope");require(rootScope&&rootScope->directory&&rootScope->owned,ErrorCode::InvalidFormat,"Automatic save root scope malformed");out.defaultScope=*rootScope;out.copyParentScope=stamp("copyParentScope");out.probeBefore=stamp("probeBefore");
    if(out.copyParentScope){require(out.copyParentScope->directory&&out.copyParentScope->owned,ErrorCode::InvalidFormat,"Automatic torrent parent scope malformed");}
    require(metadata.identity.selected==OriginalSelectedHash::V1&&metadata.identity.full.v1&&!metadata.identity.full.v2,ErrorCode::Unsupported,"Automatic recovery requires full pure v1 metadata");
    std::array<std::byte,20> hash{};std::copy(metadata.hash.bytes().begin(),metadata.hash.bytes().end(),hash.begin());
    auto initial=search::detail::subscriptionAutomaticGeometry(out.policy,metadata.layout.name,hash,metadata.layout.multifile,metadata.layout.bytes,filenameConversions,out.policy.addHashNames?search::detail::AutomaticDataLocationKind::Absent:search::detail::AutomaticDataLocationKind::Directory);require(initial.has_value(),ErrorCode::InvalidFormat,"Automatic frozen geometry malformed");
    out.probePath=out.policy.addHashNames?pathFromUtf8(metadata.layout.multifile?initial->destinationDirectory:initial->dataLocation):pathFromUtf8(out.policy.defaultSaveDirectory);
    auto geometry=search::detail::subscriptionAutomaticGeometry(out.policy,metadata.layout.name,hash,metadata.layout.multifile,metadata.layout.bytes,filenameConversions,automaticLocationKind(out.probeBefore));require(geometry.has_value(),ErrorCode::InvalidFormat,"Automatic observed geometry malformed");out.geometry=*geometry;out.copyPath=pathFromUtf8(out.geometry.torrentFile);pathShape(out.copyPath);destinationPath(pathFromUtf8(out.geometry.destinationDirectory));out.body=std::move(actualArchive);require(out.copyPath!=out.profile/"torrents"/(metadata.hash.hex()+".torrent"),ErrorCode::Unsupported,"Configured named torrent collides with the fixed canonical archive");
    require(out.copyParentScope||out.copyPath.parent_path()==out.profile/"torrents",ErrorCode::InvalidFormat,"Automatic recipe grants an uncaptured external directory");
    require(!out.copyBefore||out.copyBefore->bytes==out.body,ErrorCode::Conflict,"Automatic before artifact has different metainfo bytes");
    require(!out.created||(!out.copyBefore&&out.created->bytes==out.body),ErrorCode::InvalidFormat,"Invalid automatic created-inode witness");
#ifndef _WIN32
    require(!out.created || (out.created->info.mode & 07777) == 0600, ErrorCode::InvalidFormat,
        "Automatic artifact witness must describe a private file");
#endif
    require(out.encoded()==bytes,ErrorCode::InvalidFormat,"Automatic context is not closed/canonical");out.verify(lease,false);return out;
  }
  void verify(const ProfileLease& lease,bool strictSettings)const{
    require(profilePath(lease)==pathToUtf8(profile),ErrorCode::Conflict,"Automatic profile label changed");
    require(automaticScopeEqual(automaticDirectoryStamp(policy.defaultSaveDirectory),defaultScope),ErrorCode::Conflict,"Automatic default save scope changed");
    const auto probe=automaticProbe(probePath);require(bool(probe)==bool(probeBefore),ErrorCode::Conflict,"Automatic location kind changed");
    if(probeBefore){require(probeBefore->directory?automaticScopeEqual(*probe,*probeBefore):unchanged(*probe,*probeBefore),ErrorCode::Conflict,"Automatic saved location changed");}
    if(strictSettings){
      const auto now=read(lease.directoryHandle(),"biglybt.config",MaximumMetadata),backup=read(lease.directoryHandle(),"biglybt.config.bak",MaximumMetadata);
      require(now&&now->bytes==settings.bytes&&unchanged(now->info,settings.info)&&bool(backup)==bool(settingsBackup)&&(!backup||(backup->bytes==settingsBackup->bytes&&unchanged(backup->info,settingsBackup->info))),ErrorCode::Conflict,"Automatic policy settings changed");
      absent(lease.directoryHandle(),"biglybt.config.saving");absent(lease.directoryHandle(),"biglybt.config.saving.bak");
    }
    const auto parent=copyPath.parent_path();
    if(copyParentScope){require(automaticScopeEqual(automaticDirectoryStamp(parent),*copyParentScope),ErrorCode::Conflict,"Automatic torrent directory changed");}
    else if(parent==profile/"torrents"){
      if(const auto now=named(lease.directoryHandle(),"torrents")){require(now->directory&&now->owned,ErrorCode::Conflict,"Automatic control archive directory changed");}
    }else{fail(ErrorCode::Conflict,"Automatic torrent directory has no captured scope");}
  }
  bool createdMatches(const File& file)const{
    if(!created){return false;}const auto& a=file.info;const auto& z=created->info;
    // Rename changes ctime. Inode, owner, mode, links, bytes and mtime remain
    // fixed; the witness was persisted under the owner before this rename.
    return file.bytes==body&&sameStateIdentity(a,z)&&a.owned&&a.owner==z.owner&&a.group==z.group
        &&a.mode==z.mode&&a.links==z.links&&a.size==z.size
        &&detail::sameStateTimestamp(a.modified, z.modified);
  }
  bool artifactPresent(const ProfileLease& lease)const{
    verify(lease,false);if(!copyParentScope&&!named(lease.directoryHandle(),"torrents")){return false;}
    auto fd=directory(copyPath.parent_path());const auto name=copyPath.filename();
    const auto current=read(fd.get(),name,MaximumMetadata),staged=read(fd.get(),withSuffix(name, ".BiglyBTC.auto-saving"),MaximumMetadata);
    if(copyBefore){require(current&&current->bytes==copyBefore->bytes&&unchanged(current->info,copyBefore->info)&&!staged,ErrorCode::Conflict,"Preexisting torrent file changed");return true;}
    require(!current||createdMatches(*current),ErrorCode::Conflict,"Named torrent target lacks its durable created-inode witness");
    require(!staged||createdMatches(*staged),ErrorCode::Conflict,"Named torrent stage lacks its durable created-inode witness");
    require(!(current&&staged),ErrorCode::Conflict,"Both automatic artifact names exist");return current.has_value()&&!staged;
  }
  void stageOwned(const ProfileLease& lease,std::string_view owner){
    acquisitionOwnerCurrent(lease,owner);verify(lease,true);
    if(artifactPresent(lease)||created){return;}
    auto fd=directory(copyPath.parent_path());const auto name=copyPath.filename(),stageName=withSuffix(name, ".BiglyBTC.auto-saving");
    absent(fd.get(),name);absent(fd.get(),stageName);
    const auto written = writeStage(fd.get(), stageName, body, 0600);
    auto made = read(fd.get(), stageName, MaximumMetadata);
    require(made && unchanged(made->info, written) && made->bytes == body,
        ErrorCode::Conflict, "Automatic artifact creation raced");
    created=std::move(*made);acquisitionOwnerCurrent(lease,owner);
    // Caller MUST durably replace its closed owner plan with encoded() before
    // publish(). A crash before that update is an ambiguous stage: Review.
  }
  void publish(const ProfileLease& lease,std::string_view owner)const{
    acquisitionOwnerCurrent(lease,owner);verify(lease,true);if(artifactPresent(lease)){return;}
    require(created.has_value()&&!copyBefore,ErrorCode::Conflict,"Automatic artifact has no persisted stage identity");
    auto fd=directory(copyPath.parent_path());const auto name=copyPath.filename(),stageName=withSuffix(name, ".BiglyBTC.auto-saving");
    auto staged=read(fd.get(),stageName,MaximumMetadata);require(staged&&createdMatches(*staged),ErrorCode::Conflict,"Recorded automatic artifact stage is missing or foreign");
    acquisitionOwnerCurrent(lease,owner);verify(lease,true);absent(fd.get(),name);
    move(fd.get(), stageName, name, false, staged->info);
    require(artifactPresent(lease),ErrorCode::Conflict,"Automatic torrent postimage is not verified");acquisitionOwnerCurrent(lease,owner);
  }
  void rollback(const ProfileLease& lease,std::string_view owner)const{
    acquisitionOwnerCurrent(lease,owner);verify(lease,false);if(copyBefore){require(artifactPresent(lease),ErrorCode::Conflict,"Automatic preexisting artifact changed");return;}
    if(!copyParentScope&&!named(lease.directoryHandle(),"torrents")){return;}
    auto fd=directory(copyPath.parent_path());const auto name=copyPath.filename();
    for(const auto& leaf:{withSuffix(name, ".BiglyBTC.auto-saving"),name}){if(const auto file=read(fd.get(),leaf,MaximumMetadata)){
      require(createdMatches(*file),ErrorCode::Conflict,"Automatic artifact rollback found an unowned inode");acquisitionOwnerCurrent(lease,owner);retireFixedSubscriptionStage(fd.get(),leaf,*file);
    }}acquisitionOwnerCurrent(lease,owner);
  }
};
