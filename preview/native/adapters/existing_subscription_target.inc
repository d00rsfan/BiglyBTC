// Included only for member definitions; sealed Data is in torrent_intake.cpp.
ExistingStoppedPublicSubscriptionTarget::ExistingStoppedPublicSubscriptionTarget(std::unique_ptr<Data> data)noexcept:data_(std::move(data)){}
ExistingStoppedPublicSubscriptionTarget::ExistingStoppedPublicSubscriptionTarget(ExistingStoppedPublicSubscriptionTarget&&)noexcept=default;
ExistingStoppedPublicSubscriptionTarget& ExistingStoppedPublicSubscriptionTarget::operator=(ExistingStoppedPublicSubscriptionTarget&&)noexcept=default;
ExistingStoppedPublicSubscriptionTarget::~ExistingStoppedPublicSubscriptionTarget()=default;
const ExistingStoppedPublicSubscriptionTarget::Images& ExistingStoppedPublicSubscriptionTarget::images()const noexcept{return data_->view;}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::current(const ProfileLease& lease,std::uint64_t session,std::string_view owner)const{
  try{require(bool(data_),ErrorCode::Conflict,"Existing target observation was consumed");auto access=take(lease.sourceAccess());data_->verify(lease,session,owner);return {};}
  catch(const Failure& error){auto value=error.error;if(value.code==ErrorCode::Missing){value.code=ErrorCode::Conflict;}return std::unexpected(std::move(value));}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory rechecking existing target"});}
  catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot safely recheck existing target"});}
}
std::expected<ExistingStoppedPublicSubscriptionTarget,Error> ExistingStoppedPublicSubscriptionTarget::capture(
    const ProfileLease& lease,const TorrentIntakeMetadata& metadata,const fs::path& destination,std::uint64_t session,std::stop_token stop,std::string_view owner){
  try{
    require(session&&metadata.data_&&!stop.stop_requested(),ErrorCode::Cancelled,"Existing target capture requires a live session and metadata");
    auto access=take(lease.sourceAccess());if(owner.empty()){noAcquisitionIntent(lease);}else{acquisitionOwnerCurrent(lease,owner);}
    auto data=std::make_unique<Data>();data->lease=take(lease.retain());data->session=session;data->profile=profilePath(lease);
    data->scope(data->profile);data->scope(metadata.data_->path.parent_path());
    const auto root=data->profile,active=root/"active",archives=root/"torrents";
    for(const auto* name:ExistingRootPending){data->missing(root/name);}
    for(const auto* name:ExistingSettings){data->observe(root/name);}
    for(const auto* name:ExistingSettingsPending){data->missing(root/name);}
    const auto& fetched=data->observe(metadata.data_->path,MaximumMetadata);require(fetched&&fetched->bytes==metadata.data_->input.bytes&&unchanged(fetched->info,metadata.data_->input.info),ErrorCode::Conflict,"Fetched existing-target metainfo changed");
    const auto subject=metadataSubject(fetched->bytes,metadata.data_->filenameConversions,metadata.data_->decoderShowAll);const auto* info=subject.torrent.find("info");
    require(subject.identity.selected==OriginalSelectedHash::V1&&subject.identity.full.v1&&!subject.identity.full.v2&&subject.hash==metadata.hash()&&info&&!info->find("meta version")&&!info->find("file tree"),ErrorCode::Unsupported,"Existing target requires a full pure-v1 metainfo identity");
    const auto& inventory=data->observe(root/"downloads.config");const auto& inventoryBackup=data->observe(root/"downloads.config.bak");
    if(!inventory){require(!inventoryBackup,ErrorCode::Conflict,"Existing target refuses backup-only inventory");return std::unexpected(Error{owner.empty()?ErrorCode::Missing:ErrorCode::Conflict,"No matching actual inventory row"});}auto source=parse(inventory->bytes,Data::DocumentBytes);uniqueKeys(source.root());
    const auto library=take([&]()->std::expected<core::LibrarySnapshot,Error>{auto value=core::readLibrary(source,{Data::MaximumRows,Data::MaximumRows});if(!value||!value->issues.empty()){return std::unexpected(Error{ErrorCode::InvalidFormat,"Existing target inventory is malformed"});}return std::move(*value);}());
    const core::SavedDownload* selected=nullptr;std::set<std::string> hashes;
    for(const auto& row:library.downloads){
      require(row.hash.has_value()&&hashes.insert(row.hash->hex()).second,ErrorCode::Conflict,"Existing target inventory has unresolved/duplicate identities");
      const auto key=row.hash->bytes(),wanted=subject.hash.bytes();require(key.size()!=32||!std::equal(wanted.begin(),wanted.end(),key.begin()),ErrorCode::Conflict,"Existing target has a selected20/full32 alias collision");
      data->knownHashes.push_back(row.hash->hex());
      if(*row.hash==subject.hash){require(!selected,ErrorCode::Conflict,"Existing target is not unique");selected=&row;}

    }
    if(!selected){return std::unexpected(Error{owner.empty()?ErrorCode::Missing:ErrorCode::Conflict,"No matching actual inventory row"});}
    pathShape(destination);std::error_code pathError;require(fs::canonical(destination,pathError)==destination&&!pathError,ErrorCode::UnsafePath,"Existing target destination is not canonical");data->scope(destination);
    require(!metadata.data_->initialDestinations.size(),ErrorCode::Unsupported,"Existing target refuses metainfo destination overrides");
    if(const auto* privateFlag=info->find("private")){require(privateFlag->type==Node::Type::Integer&&privateFlag->integer==0,ErrorCode::Unsupported,"Private existing targets are unsupported");}
    require(selected->persistent&&selected->originalState==70&&selected->startup==core::StartupState::Stopped&&!selected->forceStart,ErrorCode::Unsupported,"Existing target must be persistent stopped70 and unforced");
    data->scope(active);data->scope(archives);
    for(const auto& row:library.downloads){
      const auto stateName=row.hash->hex()+".dat";require(data->observe(active/stateName).has_value(),ErrorCode::Conflict,"Alias checking requires every primary state");data->observe(active/(stateName+".bak"));
      for(const auto* suffix:ExistingStatePending){data->missing(active/(stateName+suffix));}
    }
    for(const auto& pause:library.paused){require(pause.hash!=subject.hash,ErrorCode::Unsupported,"Paused existing target is unsupported");}
    const Node* row=nullptr;if(library.legacyMap){row=&source.root().children.at(selected->sourceIndex*2+1);}else{const auto* rows=source.find("downloads");require(rows&&rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Existing target rows are missing");row=&rows->children.at(selected->sourceIndex);}
    for(const auto* key:{VerifiedSeedKey,VerifiedSeedGoalKey,AllocationModeKey,AllocationStateKey,AllocationSnapshotKey}){require(!row->find(key),ErrorCode::Unsupported,"Existing target has verification/allocation controller intent");}
    const auto resolved=take(resolveOriginalSaveRoot(*selected,subject.layout));require(!selected->legacySavePath&&resolved.saveDirectory==destination.native()&&resolved.saveFile&&*resolved.saveFile==subject.layout.name,ErrorCode::Unsupported,"Existing target needs its unchanged canonical default destination");
    const fs::path archivePath=selected->torrentFile;require(archivePath==archives/(subject.hash.hex()+".torrent"),ErrorCode::Unsupported,"Existing target needs its canonical primary profile archive");
    const auto& archive=data->observe(archivePath,MaximumMetadata);const auto& archiveBackup=data->observe(fs::path(archivePath.native()+".bak"),MaximumMetadata);require(archive.has_value(),ErrorCode::Conflict,"Existing target has no primary archive");
    for(const auto* suffix:ExistingStatePending){data->missing(fs::path(archivePath.native()+suffix));}
    const auto archiveSubject=metadataSubject(archive->bytes,metadata.data_->filenameConversions,metadata.data_->decoderShowAll);
    existingSameLayout(archiveSubject.layout,subject.layout);take(matchOriginalFullHashes(archiveSubject.identity.full,subject.identity.full));require(archiveSubject.identity.selected==OriginalSelectedHash::V1&&archiveSubject.hash==subject.hash,ErrorCode::Conflict,"Existing archive selected identity differs");
    data->rawInfo=std::string(take(originalTorrentInfoSubject(subject.torrent)));require(take(originalTorrentInfoSubject(archiveSubject.torrent))==data->rawInfo,ErrorCode::Conflict,"Existing archive raw info differs from fetched metainfo");
    const auto statePath=active/(subject.hash.hex()+".dat");data->statePath=statePath;data->archivePath=archivePath;const auto& state=data->observe(statePath);const auto& stateBackup=data->observe(fs::path(statePath.native()+".bak"));
    require((state->info.st_mode&07000)==0,ErrorCode::Unsupported,"Existing fixed state does not support special permission bits");
    auto envelope=EncodedDocument::parse(state->bytes,compatibleConfigLimits());require(envelope.has_value(),ErrorCode::InvalidFormat,"Existing primary state cannot be decoded");const auto& stateDoc=envelope->document();uniqueKeys(stateDoc.root());
    const auto stateLayout=compat::originalLayout(stateDoc,100000,metadata.data_->filenameConversions,metadata.data_->decoderShowAll);require(stateLayout.has_value(),ErrorCode::InvalidFormat,"Existing state layout is unavailable");existingSameLayout(*stateLayout,subject.layout);
    require(take(originalTorrentInfoSubject(stateDoc))==data->rawInfo,ErrorCode::Conflict,"Existing state raw info differs from fetched metainfo");const auto stateIdentity=take(selectOriginalTorrentIdentity(stateDoc,{data->rawInfo,subject.identity.full}));
    take(matchOriginalFullHashes(stateIdentity.full,subject.identity.full));require(stateIdentity.selected==OriginalSelectedHash::V1&&stateIdentity.originalKey==subject.hash,ErrorCode::Conflict,"Existing state identity is an alias");
    const auto* attributes=stateDoc.find("attributes");require(attributes&&attributes->type==Node::Type::Dictionary,ErrorCode::Unsupported,"Existing target requires authoritative primary attributes");const auto* networks=attributes->find("networks");
    require(networks&&networks->type==Node::Type::List&&networks->children.size()==1&&networks->children[0].type==Node::Type::Bytes&&networks->children[0].bytes=="Public",ErrorCode::Unsupported,"Existing target must be explicitly Public");
    if(const auto* links=attributes->find("filelinks2")){require(links->type==Node::Type::List&&links->children.empty(),ErrorCode::Unsupported,"Existing linked files are unsupported");}
    if(const auto* alternate=attributes->find("dnd_al")){require(alternate->type==Node::Type::Bytes&&alternate->bytes.empty(),ErrorCode::Unsupported,"Existing DND aliases are unsupported");}
    data->missing(active/subject.hash.hex());data->attributes=raw(stateDoc,*attributes);
    for(const auto* name:{"cache.dat","cache.dat.bak"}){data->observe(active/name);}
    for(const auto* name:ExistingCachePending){data->missing(active/name);}
    require(data->inputs.at(active/"cache.dat").has_value()||!data->inputs.at(active/"cache.dat.bak"),ErrorCode::Conflict,"Existing target refuses backup-only cache resolution");cacheAbsent(lease.directoryHandle(),subject.hash);
    if(const auto& cache=data->inputs.at(active/"cache.dat")){auto decoded=EncodedDocument::parse(cache->bytes,compatibleConfigLimits());require(decoded.has_value(),ErrorCode::InvalidFormat,"Existing cache cannot be decoded");auto cached=core::readStateCache(decoded->document());require(cached&&cached->issues.empty(),ErrorCode::InvalidFormat,"Existing cache is malformed");for(const auto& [key,value]:cached->torrents){static_cast<void>(value);require(key.size()!=64||!key.starts_with(subject.hash.hex()),ErrorCode::Conflict,"Existing target cache has a full32 prefix collision");}}
    auto store=take(TorrentStateStore::open(lease));for(const auto& saved:library.downloads){auto actual=take(store.load(lease,*saved.hash));require(!actual.recoveredBackup(),ErrorCode::Conflict,"Alias checking refuses backup-state fallback");}
    auto otherInventory=source;
    if(library.legacyMap){std::string encoded="d";for(std::size_t n=0;n<source.root().children.size();n+=2){if(n/2!=selected->sourceIndex){encoded+=raw(source,source.root().children[n])+raw(source,source.root().children[n+1]);}}data->independence.before=encoded+'e';}
    else{const auto* rows=source.find("downloads");std::string encoded="l";for(std::size_t n=0;n<rows->children.size();++n){if(n!=selected->sourceIndex){encoded+=raw(source,rows->children[n]);}}put(otherInventory,"downloads",encoded+'e');data->independence.before=otherInventory.serialize();}
    require(subject.layout.files.size()<=4096,ErrorCode::Unsupported,"Existing target file scope exceeds bound");data->independence.hash=subject.hash;data->independence.metadata=fetched->bytes;data->independence.destination=destination.native();data->independence.profile=root.native();data->independence.filenameConversions=metadata.filenameConversions();data->independence.decoderShowAll=metadata.decoderShowAll();
    IntakeFileOptions aliasCheck{};const auto paths=physicalIntakePaths(subject.layout,destination.native(),subject.layout.name,{},metadata.filenameConversions());for(std::size_t n=0;n<paths.size();++n){if(!subject.layout.files[n].padding&&!paths[n].empty()){aliasCheck.destinations.push_back({static_cast<std::uint32_t>(n),paths[n].native()});}}
    require(!aliasCheck.destinations.empty(),ErrorCode::Unsupported,"Existing target has no ordinary payload leaf");data->independence.options=std::move(aliasCheck);
    const auto view=[](const std::optional<File>& value)->std::optional<std::string_view>{return value?std::optional<std::string_view>{value->bytes}:std::nullopt;};
    data->view={subject.hash,subject.identity,data->rawInfo,inventory->bytes,archive->bytes,state->bytes,data->attributes,state->bytes,state->bytes,view(inventoryBackup),view(archiveBackup),view(stateBackup),destination};
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Existing target capture cancelled");data->verify(lease,session,owner);
    // The network temp is authenticated input value, not a permanent target
    // capability. After this full capture, cleanup/crash recovery needs only
    // stored geometry and the actual archive/state/inventory target.
    if(metadata.data_->path!=archivePath&&metadata.data_->path!=statePath&&metadata.data_->path!=root/"downloads.config"){
      data->inputs.erase(metadata.data_->path);const auto tempParent=metadata.data_->path.parent_path();
      data->directories.erase(std::remove_if(data->directories.begin(),data->directories.end(),[&](const auto& observed){return observed.path==tempParent&&observed.path!=root&&observed.path!=active&&observed.path!=archives&&observed.path!=destination;}),data->directories.end());
    }
    data->bytes=data->independence.metadata.size();for(const auto& [path,value]:data->inputs){static_cast<void>(path);if(value){require(value->bytes.size()<=Data::TotalBytes-data->bytes,ErrorCode::Unsupported,"Existing frozen target exceeds total bound");data->bytes+=value->bytes.size();}}
    data->closedPaths();return ExistingStoppedPublicSubscriptionTarget(std::move(data));
  }catch(const Failure& error){auto value=error.error;if(value.code==ErrorCode::Missing){value.code=ErrorCode::Conflict;}return std::unexpected(std::move(value));}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory capturing existing target"});}
  catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot safely capture existing target"});}
}


std::expected<ExistingStoppedPublicSubscriptionTarget,Error> ExistingStoppedPublicSubscriptionTarget::captureAutomatic(
    const ProfileLease& lease,const TorrentIntakeMetadata& metadata,std::uint64_t session,std::stop_token stop){
  try{
    require(metadata.data_&&!stop.stop_requested(),ErrorCode::Cancelled,"Automatic existing capture needs actual metainfo");fs::path destination;
    {
      auto access=take(lease.sourceAccess());noAcquisitionIntent(lease);const auto primary=read(lease.directoryHandle(),"downloads.config",Data::DocumentBytes),backup=read(lease.directoryHandle(),"downloads.config.bak",Data::DocumentBytes);
      if(!primary){require(!backup,ErrorCode::Conflict,"Automatic existing target has backup-only inventory");return std::unexpected(Error{ErrorCode::Missing,"No matching actual inventory row"});}
      const auto source=parse(primary->bytes,Data::DocumentBytes);const auto library=core::readLibrary(source,{Data::MaximumRows,Data::MaximumRows});require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Automatic existing inventory is malformed");
      const core::SavedDownload* selected=nullptr;for(const auto& row:library->downloads){if(row.hash&&*row.hash==metadata.hash()){require(!selected,ErrorCode::Conflict,"Automatic existing target is duplicated");selected=&row;}}
      if(!selected){return std::unexpected(Error{ErrorCode::Missing,"No matching actual inventory row"});}
      const auto resolved=take(resolveOriginalSaveRoot(*selected,metadata.layout()));destination=resolved.saveDirectory;
    }
    auto target=take(capture(lease,metadata,destination,session,stop));
    const auto subject=metadataSubject(metadata.data_->input.bytes,metadata.filenameConversions(),metadata.decoderShowAll());
    target.data_->automatic=AutomaticIntakeContext::capture(lease,subject,std::string(target.images().archive),metadata.filenameConversions());
    target.data_->verify(lease,session,{});return target;
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot capture actual retained automatic target"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::prepareAutomatic(FixedSubscriptionDownloadPreparation preparation){
  if(!data_||!data_->automatic){return std::unexpected(Error{ErrorCode::Conflict,"Actual automatic target context missing"});}return prepare(std::move(preparation));
}
const search::detail::SubscriptionAutomaticPolicyValues* ExistingStoppedPublicSubscriptionTarget::automaticPolicy()const noexcept{return data_&&data_->automatic?&data_->automatic->policy:nullptr;}
const search::detail::SubscriptionAutomaticGeometry* ExistingStoppedPublicSubscriptionTarget::automaticGeometry()const noexcept{return data_&&data_->automatic?&data_->automatic->geometry:nullptr;}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::prepareAutomaticArtifacts(const ProfileLease& lease,std::uint64_t session,std::string_view owner){
  try{require(data_&&data_->automatic&&data_->preparation,ErrorCode::Conflict,"Automatic existing target is not sealed");data_->verify(lease,session,owner,true,true);data_->automatic->stageOwned(lease,owner);if(!data_->automatic->copyParentScope){data_->automatic->copyParentScope=automaticDirectoryStamp(data_->automatic->copyPath.parent_path());}data_->recipe=data_->encodeRecipe();require(data_->recipe.size()<=MaximumJournal,ErrorCode::Unsupported,"Automatic target created-inode recipe exceeds bound");return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic existing artifact preparation requires review"});}
}
std::expected<bool,Error> ExistingStoppedPublicSubscriptionTarget::automaticArtifactPresent(const ProfileLease& lease,std::uint64_t session,std::string_view owner)const{
  try{require(data_&&data_->automatic,ErrorCode::Conflict,"Automatic existing context missing");data_->verify(lease,session,owner,false,true);return data_->automatic->artifactPresent(lease);}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot observe automatic existing artifact"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::publishAutomaticArtifact(const ProfileLease& lease,std::uint64_t session,std::string_view owner){
  try{require(data_&&data_->automatic,ErrorCode::Conflict,"Automatic existing context missing");data_->verify(lease,session,owner,true,true);automaticOwnerRecipe(owner,data_->recipe);data_->automatic->publish(lease,owner);return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic existing artifact needs review"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::rollbackAutomaticArtifact(const ProfileLease& lease,std::uint64_t session,std::string_view owner){
  try{require(data_&&data_->automatic,ErrorCode::Conflict,"Automatic existing context missing");data_->verify(lease,session,owner,false,true);automaticOwnerRecipe(owner,data_->recipe);data_->automatic->rollback(lease,owner);return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic existing artifact rollback needs review"});}
}

std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::prepare(FixedSubscriptionDownloadPreparation preparation){
  try{
    require(data_&&!data_->preparation,ErrorCode::Conflict,"Existing-target preparation is absent or already sealed");auto access=take(data_->lease->sourceAccess());data_->verify(*data_->lease,data_->session,{});
    data_->states=fixedSubscriptionStateImages(std::string(data_->view.state),preparation,false);data_->preparation=std::move(preparation);data_->bindViews();data_->closedPaths();auto recipe=data_->encodeRecipe();require(recipe.size()<=MaximumJournal,ErrorCode::Unsupported,"Existing target recipe exceeds bound");data_->recipe=std::move(recipe);return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory preparing existing target"});}
  catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot prepare existing target state"});}
}
std::string_view ExistingStoppedPublicSubscriptionTarget::frozenRecipe()const noexcept{return data_?std::string_view(data_->recipe):std::string_view{};}
const FixedSubscriptionDownloadPreparation* ExistingStoppedPublicSubscriptionTarget::fixedPreparation()const noexcept{return data_&&data_->preparation?&*data_->preparation:nullptr;}
std::expected<ExistingStoppedPublicSubscriptionTarget::StateObservation,Error> ExistingStoppedPublicSubscriptionTarget::inspectState(const ProfileLease& lease,std::uint64_t session,std::string_view owner)const{
  try{
    require(data_&&data_->preparation&&!data_->recipe.empty()&&!owner.empty(),ErrorCode::Conflict,"Existing-target owned recipe is not sealed");auto access=take(lease.sourceAccess());data_->verify(lease,session,owner,false,true);auto active=directory(data_->statePath.parent_path());
    const auto observed=observeFixedSubscriptionState(active.get(),data_->statePath.filename().native(),data_->inputs.at(fs::path(data_->statePath.native()+".bak")),{data_->states.base,data_->states.prepared,data_->states.associated});
    const auto& original=*data_->inputs.at(data_->statePath);require(observed.primary.info.st_mode==original.info.st_mode&&observed.primary.info.st_gid==original.info.st_gid,ErrorCode::Conflict,"Existing state permissions changed");
    return StateObservation{observed.primaryMatches,observed.savingMatches};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Existing target state needs preserved review"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::publishPreparedState(const ProfileLease& lease,std::uint64_t session,std::string_view owner){
  try{
    require(data_&&data_->preparation&&!data_->recipe.empty(),ErrorCode::Conflict,"Existing target fixed preparation missing");auto access=take(lease.sourceAccess(true));data_->verify(lease,session,owner,true,true);const auto phase=take(inspectState(lease,session,owner));require((phase.primaryIs(StatePhase::Base)||phase.primaryIs(StatePhase::Prepared))&&(!phase.savingMatches||phase.savingIs(StatePhase::Prepared)),ErrorCode::Conflict,"Existing forward state crossed another fixed phase");auto active=directory(data_->statePath.parent_path());
    publishFixedSubscriptionState(lease,active.get(),data_->statePath.filename().native(),data_->inputs.at(fs::path(data_->statePath.native()+".bak")),{data_->states.base,data_->states.prepared,data_->states.associated},1,owner);
    data_->verify(lease,session,owner,true,true);static_cast<void>(take(inspectState(lease,session,owner)));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Existing target preparation publication needs review"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::publishAssociationState(const ProfileLease& lease,std::uint64_t session,std::string_view owner,void(*observer)(SubscriptionDownloadStateStage)){
  try{
    require(data_&&data_->preparation&&!data_->recipe.empty(),ErrorCode::Conflict,"Existing target fixed preparation missing");auto access=take(lease.sourceAccess(true));data_->verify(lease,session,owner,true,true);const auto phase=take(inspectState(lease,session,owner));require((phase.primaryIs(StatePhase::Prepared)||phase.primaryIs(StatePhase::Associated))&&(!phase.savingMatches||phase.savingIs(StatePhase::Associated)),ErrorCode::Conflict,"Existing forward state crossed another fixed phase");auto active=directory(data_->statePath.parent_path());
    publishFixedSubscriptionState(lease,active.get(),data_->statePath.filename().native(),data_->inputs.at(fs::path(data_->statePath.native()+".bak")),{data_->states.base,data_->states.prepared,data_->states.associated},2,owner,observer);
    data_->verify(lease,session,owner,true,true);static_cast<void>(take(inspectState(lease,session,owner)));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Existing target callback publication needs review"});}
}
std::expected<void,Error> ExistingStoppedPublicSubscriptionTarget::rollbackState(const ProfileLease& lease,std::uint64_t session,std::string_view owner){
  try{
    require(data_&&data_->preparation&&!data_->recipe.empty(),ErrorCode::Conflict,"Existing target fixed preparation missing");auto access=take(lease.sourceAccess(true));data_->verify(lease,session,owner,false,true);static_cast<void>(take(inspectState(lease,session,owner)));auto active=directory(data_->statePath.parent_path());
    publishFixedSubscriptionState(lease,active.get(),data_->statePath.filename().native(),data_->inputs.at(fs::path(data_->statePath.native()+".bak")),{data_->states.base,data_->states.prepared,data_->states.associated},0,owner);
    data_->verify(lease,session,owner,false,true);static_cast<void>(take(inspectState(lease,session,owner)));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Existing target rollback needs preserved review"});}
}


std::expected<ExistingStoppedPublicSubscriptionTarget,Error> ExistingStoppedPublicSubscriptionTarget::restore(
    const ProfileLease& lease,const TorrentIntakeMetadata& metadata,const fs::path& destination,std::uint64_t session,std::string recipe,std::string_view owner){
  try{
    require(session&&metadata.data_&&!recipe.empty()&&recipe.size()<=MaximumJournal,ErrorCode::InvalidFormat,"Existing target recovery inputs are missing or unbounded");auto access=take(lease.sourceAccess());existingReadonlyOwner(lease,owner);
    const auto wrapper=parse(recipe,MaximumJournal);uniqueKeys(wrapper.root());
    if(const auto* schema=wrapper.find("schema");schema&&schema->type==Node::Type::Bytes&&schema->bytes=="BiglyBTC.ExistingStoppedSubscriptionTarget.V2"){
      require(wrapper.root().children.size()==6,ErrorCode::InvalidFormat,"Unknown automatic existing target wrapper");
      const auto ordinary=text(wrapper.find("target")),context=text(wrapper.find("automatic"));
      auto target=take(restore(lease,metadata,destination,session,ordinary,owner));
      const auto subject=metadataSubject(target.data_->independence.metadata,target.data_->independence.filenameConversions?std::optional<std::string_view>{*target.data_->independence.filenameConversions}:std::nullopt,target.data_->independence.decoderShowAll.value_or(false));
      target.data_->automatic=AutomaticIntakeContext::restore(lease,subject,std::string(target.images().archive),target.data_->independence.filenameConversions.value_or(std::string(compat::OriginalDefaultFilenameConversions)),context);
      target.data_->recipe=target.data_->encodeRecipe();require(target.data_->recipe==recipe,ErrorCode::InvalidFormat,"Automatic existing target recipe is not canonical");return target;
    }
    const auto& record=wrapper;require(record.root().children.size()==18,ErrorCode::InvalidFormat,"Unknown existing target recipe field");
    const auto textField=[&](std::string_view key){const auto* value=record.find(key);require(value&&value->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Existing target recipe field missing");return std::string(value->bytes);};
    require(textField("schema")=="BiglyBTC.ExistingStoppedSubscriptionTarget.V1",ErrorCode::Unsupported,"Unknown existing-target recipe version");
    auto data=std::make_unique<Data>();data->lease=take(lease.retain());data->session=session;data->profile=profilePath(lease);require(textField("profile")==data->profile.native()&&textField("destination")==destination.native(),ErrorCode::Conflict,"Existing target recovery path changed");pathShape(destination);std::error_code pathError;require(fs::canonical(destination,pathError)==destination&&!pathError,ErrorCode::Conflict,"Existing target destination changed");
    const auto* showAll=record.find("showAll"),*fixed=record.find("preparation"),*files=record.find("files"),*scopes=record.find("directories");require(showAll&&showAll->type==Node::Type::Integer&&(showAll->integer==0||showAll->integer==1)&&fixed&&files&&files->type==Node::Type::Dictionary&&scopes&&scopes->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Existing target recovery structure invalid");
    const auto conversions=textField("conversions");const bool decoderShowAll=showAll->integer==1;const auto fetched=textField("metadata");require(fetched.size()<=Data::TotalBytes,ErrorCode::Unsupported,"Existing recovery metainfo exceeds total bound");data->bytes=fetched.size();const auto subject=metadataSubject(fetched,conversions,decoderShowAll);const auto* info=subject.torrent.find("info");
    require(subject.identity.selected==OriginalSelectedHash::V1&&subject.identity.full.v1&&!subject.identity.full.v2&&subject.hash==metadata.hash()&&info&&!info->find("meta version")&&!info->find("file tree")&&initialFileDestinations(subject).empty(),ErrorCode::Unsupported,"Existing target recovery requires its actual pure-v1 identity");
    if(const auto* privateFlag=info->find("private")){require(privateFlag->type==Node::Type::Integer&&privateFlag->integer==0,ErrorCode::Unsupported,"Private existing target recovery unsupported");}
    data->view.hash=subject.hash;data->view.identity=subject.identity;data->view.destination=destination;data->rawInfo=std::string(take(originalTorrentInfoSubject(subject.torrent)));data->statePath=data->profile/"active"/(subject.hash.hex()+".dat");data->archivePath=data->profile/"torrents"/(subject.hash.hex()+".torrent");
    require(files->children.size()/2<32768,ErrorCode::Unsupported,"Existing recovery candidate set exceeds bound");
    for(std::size_t n=0;n<files->children.size();n+=2){const fs::path path(std::string(files->children[n].bytes));pathShape(path);const auto relative=path.lexically_relative(data->profile);require(!relative.empty()&&*relative.begin()!="..",ErrorCode::UnsafePath,"Existing recovery observation escaped profile");const auto& value=files->children[n+1];std::optional<File> file;
      if(value.type==Node::Type::Integer){require(value.integer==0,ErrorCode::InvalidFormat,"Existing missing observation malformed");}
      else{require(value.type==Node::Type::Dictionary&&value.children.size()==4,ErrorCode::InvalidFormat,"Existing observation malformed");const auto* bytes=value.find("bytes"),*stamp=value.find("stamp");require(bytes&&bytes->type==Node::Type::Bytes&&stamp,ErrorCode::InvalidFormat,"Existing observation data missing");const auto maximum=(path==data->archivePath||path==fs::path(data->archivePath.native()+".bak"))?MaximumMetadata:Data::DocumentBytes;require(bytes->bytes.size()<=maximum,ErrorCode::Unsupported,"Existing recovery document exceeds captured bound");auto infoValue=existingStampValue(*stamp);require(S_ISREG(infoValue.st_mode)&&infoValue.st_uid==::geteuid()&&infoValue.st_nlink==1&&infoValue.st_size>=0&&static_cast<std::uint64_t>(infoValue.st_size)==bytes->bytes.size()&&bytes->bytes.size()<=Data::TotalBytes-data->bytes,ErrorCode::InvalidFormat,"Existing observation ownership/bounds invalid");data->bytes+=bytes->bytes.size();file=File{std::string(bytes->bytes),infoValue};}
      require(data->inputs.emplace(path,std::move(file)).second,ErrorCode::InvalidFormat,"Duplicate existing observation path");
    }
    const auto inventoryAt=data->inputs.find(data->profile/"downloads.config");require(inventoryAt!=data->inputs.end()&&inventoryAt->second,ErrorCode::InvalidFormat,"Existing recovery inventory missing");auto source=parse(inventoryAt->second->bytes,Data::DocumentBytes);uniqueKeys(source.root());const auto library=core::readLibrary(source,{Data::MaximumRows,Data::MaximumRows});require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Existing recovery inventory invalid");
    const core::SavedDownload* selected=nullptr;std::set<std::string> hashes;for(const auto& row:library->downloads){require(row.hash&&hashes.insert(row.hash->hex()).second,ErrorCode::Conflict,"Existing recovery identities unresolved/duplicated");const auto key=row.hash->bytes(),wanted=subject.hash.bytes();require(key.size()!=32||!std::equal(wanted.begin(),wanted.end(),key.begin()),ErrorCode::Conflict,"Existing recovery has a full32 prefix collision");data->knownHashes.push_back(row.hash->hex());if(*row.hash==subject.hash){selected=&row;}}
    require(selected&&selected->persistent&&selected->originalState==70&&selected->startup==core::StartupState::Stopped&&!selected->forceStart,ErrorCode::Conflict,"Existing recovery target is no longer stopped/unforced");for(const auto& pause:library->paused){require(pause.hash!=subject.hash,ErrorCode::Conflict,"Existing recovery target is paused");}
    const Node* selectedRow=library->legacyMap?&source.root().children.at(selected->sourceIndex*2+1):&source.find("downloads")->children.at(selected->sourceIndex);for(const auto* key:{VerifiedSeedKey,VerifiedSeedGoalKey,AllocationModeKey,AllocationStateKey,AllocationSnapshotKey}){require(!selectedRow->find(key),ErrorCode::Conflict,"Existing recovery target has controller intent");}
    const auto resolved=take(resolveOriginalSaveRoot(*selected,subject.layout));require(!selected->legacySavePath&&resolved.saveFile&&resolved.saveDirectory==destination.native()&&*resolved.saveFile==subject.layout.name&&fs::path(selected->torrentFile)==data->archivePath&&!intersectsSaveName(destination/subject.layout.name,data->profile),ErrorCode::Conflict,"Existing recovery target location changed");
    data->closedPaths();const auto& archive=data->inputs.at(data->archivePath),&base=data->inputs.at(data->statePath);require(archive&&base&&(base->info.st_mode&07000)==0&&metadata.data_->path==data->archivePath&&metadata.data_->input.bytes==archive->bytes&&unchanged(metadata.data_->input.info,archive->info),ErrorCode::Conflict,"Existing recovery needs its actual canonical archive descriptor");
    const auto archived=metadataSubject(archive->bytes,conversions,decoderShowAll);existingSameLayout(archived.layout,subject.layout);require(archived.identity.selected==OriginalSelectedHash::V1&&archived.hash==subject.hash&&take(originalTorrentInfoSubject(archived.torrent))==data->rawInfo,ErrorCode::Conflict,"Existing archive differs from full frozen metainfo identity");take(matchOriginalFullHashes(archived.identity.full,subject.identity.full));
    auto state=EncodedDocument::parse(base->bytes,compatibleConfigLimits());require(state.has_value(),ErrorCode::InvalidFormat,"Existing recovery base state malformed");uniqueKeys(state->document().root());require(take(originalTorrentInfoSubject(state->document()))==data->rawInfo,ErrorCode::Conflict,"Existing recovery base raw info differs");const auto identity=take(selectOriginalTorrentIdentity(state->document(),{data->rawInfo,subject.identity.full}));require(identity.selected==OriginalSelectedHash::V1&&identity.originalKey==subject.hash,ErrorCode::Conflict,"Existing recovery base identity is aliased");take(matchOriginalFullHashes(identity.full,subject.identity.full));
    const auto stateLayout=compat::originalLayout(state->document(),100000,conversions,decoderShowAll);require(stateLayout.has_value(),ErrorCode::InvalidFormat,"Existing recovery geometry unavailable");existingSameLayout(*stateLayout,subject.layout);const auto* attributes=state->document().find("attributes");require(attributes&&attributes->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Existing recovery attributes missing");const auto* networks=attributes->find("networks");require(networks&&networks->type==Node::Type::List&&networks->children.size()==1&&networks->children[0].type==Node::Type::Bytes&&networks->children[0].bytes=="Public",ErrorCode::Conflict,"Existing recovery target is not explicitly Public");
    if(const auto* links=attributes->find("filelinks2")){require(links->type==Node::Type::List&&links->children.empty(),ErrorCode::Conflict,"Existing recovery target has file aliases");}if(const auto* alternate=attributes->find("dnd_al")){require(alternate->type==Node::Type::Bytes&&alternate->bytes.empty(),ErrorCode::Conflict,"Existing recovery target has DND alias");}
    const auto frozenAbsent=[&](const fs::path& path){const auto found=data->inputs.find(path);require(found!=data->inputs.end()&&!found->second,ErrorCode::InvalidFormat,"Existing recipe contains a pending recovery observation");};for(const auto* name:ExistingRootPending){frozenAbsent(data->profile/name);}for(const auto* name:ExistingSettingsPending){frozenAbsent(data->profile/name);}for(const auto& hash:data->knownHashes){for(const auto* suffix:ExistingStatePending){frozenAbsent(data->profile/"active"/(hash+".dat"+suffix));}}for(const auto* suffix:ExistingStatePending){frozenAbsent(fs::path(data->archivePath.native()+suffix));}for(const auto* name:ExistingCachePending){frozenAbsent(data->profile/"active"/name);}frozenAbsent(data->profile/"active"/subject.hash.hex());
    const auto& cache=data->inputs.at(data->profile/"active/cache.dat"),&cacheBackup=data->inputs.at(data->profile/"active/cache.dat.bak");require(cache||!cacheBackup,ErrorCode::Conflict,"Existing recovery has backup-only cache");if(cache){auto decoded=EncodedDocument::parse(cache->bytes,compatibleConfigLimits());require(decoded.has_value(),ErrorCode::InvalidFormat,"Existing recovery cache invalid");auto values=core::readStateCache(decoded->document());require(values&&values->issues.empty()&&!values->torrents.contains(subject.hash.hex()),ErrorCode::Conflict,"Existing recovery attributes are shadowed by cache");for(const auto& [key,value]:values->torrents){static_cast<void>(value);require(key.size()!=64||!key.starts_with(subject.hash.hex()),ErrorCode::Conflict,"Existing recovery cache has a prefix collision");}}
    const std::set<fs::path> expectedScopes{data->profile,data->profile/"active",data->profile/"torrents",destination};std::set<fs::path> actualScopes;for(std::size_t n=0;n<scopes->children.size();n+=2){fs::path path(std::string(scopes->children[n].bytes));require(expectedScopes.contains(path)&&actualScopes.insert(path).second,ErrorCode::InvalidFormat,"Unknown existing recovery directory scope");auto saved=existingStampValue(scopes->children[n+1]);require(S_ISDIR(saved.st_mode)&&saved.st_uid==::geteuid(),ErrorCode::InvalidFormat,"Existing recovery directory ownership invalid");auto held=directory(path);data->directories.push_back({path,std::move(held),saved});}require(actualScopes==expectedScopes,ErrorCode::InvalidFormat,"Missing existing recovery directory scope");
    auto otherInventory=source;if(library->legacyMap){std::string encoded="d";for(std::size_t n=0;n<source.root().children.size();n+=2){if(n/2!=selected->sourceIndex){encoded+=raw(source,source.root().children[n])+raw(source,source.root().children[n+1]);}}data->independence.before=encoded+'e';}else{const auto* rows=source.find("downloads");std::string encoded="l";for(std::size_t n=0;n<rows->children.size();++n){if(n!=selected->sourceIndex){encoded+=raw(source,rows->children[n]);}}put(otherInventory,"downloads",encoded+'e');data->independence.before=otherInventory.serialize();}
    require(subject.layout.files.size()<=4096,ErrorCode::Unsupported,"Existing recovery file count exceeds bound");data->independence.hash=subject.hash;data->independence.metadata=fetched;data->independence.destination=destination.native();data->independence.profile=data->profile.native();data->independence.filenameConversions=conversions;data->independence.decoderShowAll=decoderShowAll;IntakeFileOptions aliasCheck{};const auto paths=physicalIntakePaths(subject.layout,destination.native(),subject.layout.name,{},conversions);for(std::size_t n=0;n<paths.size();++n){if(!subject.layout.files[n].padding&&!paths[n].empty()){aliasCheck.destinations.push_back({static_cast<std::uint32_t>(n),paths[n].native()});}}require(!aliasCheck.destinations.empty(),ErrorCode::Unsupported,"Existing recovery has no payload leaf");data->independence.options=std::move(aliasCheck);
    data->preparation=fixedPreparationValue(*fixed);data->states=fixedSubscriptionStateImages(base->bytes,*data->preparation,false);data->bindViews();data->recipe=data->encodeRecipe();require(data->recipe==recipe,ErrorCode::InvalidFormat,"Existing recipe is not closed/canonical");
    // Readonly owner-candidate validation precedes coordinator promotion. This
    // constructs no forward policy grant and accepts no generic state override.
    data->checkDirectories();data->checkInputs(false,true);auto active=directory(data->statePath.parent_path());const auto actual=observeFixedSubscriptionState(active.get(),data->statePath.filename().native(),data->inputs.at(fs::path(data->statePath.native()+".bak")),{data->states.base,data->states.prepared,data->states.associated});require(actual.primary.info.st_mode==base->info.st_mode&&actual.primary.info.st_gid==base->info.st_gid,ErrorCode::Conflict,"Existing recovery state permissions changed");data->checkInputs(false,true);data->checkDirectories();existingReadonlyOwner(lease,owner);
    return ExistingStoppedPublicSubscriptionTarget(std::move(data));
  }catch(const Failure& error){auto value=error.error;if(value.code==ErrorCode::Missing){value.code=ErrorCode::Conflict;}return std::unexpected(std::move(value));}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory restoring existing target"});}
  catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Existing target recovery requires preserved review"});}
}
