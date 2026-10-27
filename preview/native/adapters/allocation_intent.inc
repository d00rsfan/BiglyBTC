// Included after verified_seeding.inc; shares its exact list-row projection.
namespace {
std::optional<std::pair<core::TorrentAllocationMode,AllocationState>> allocationFields(const Node& row){
  const auto* mode=row.find(AllocationModeKey);const auto* phase=row.find(AllocationStateKey);
  if(!mode&&!phase){return {};}
  require(mode&&phase&&mode->type==Node::Type::Integer&&phase->type==Node::Type::Integer
      &&(mode->integer==5||mode->integer==6)&&phase->integer>=1&&phase->integer<=3,
      ErrorCode::InvalidFormat,"Invalid bounded allocation intent");
  return std::pair{static_cast<core::TorrentAllocationMode>(mode->integer),static_cast<AllocationState>(phase->integer)};
}
}
std::expected<std::optional<AllocationIntent>,Error> readAllocationIntent(const ProfileLease& lease,const core::TorrentHash& hash){
  try{
    auto file=take(ConfigFile::load(lease,"downloads.config"));const auto library=take(core::readLibrary(file.document()).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid allocation inventory"};}));
    const Node* chosen=nullptr;
    for(const auto& saved:library.downloads){if(saved.hash&&*saved.hash==hash){const auto* rows=file.document().find("downloads");const auto* row=rows?&rows->children.at(saved.sourceIndex):file.document().find(*saved.legacyKey);
      if(row&&(row->find(AllocationModeKey)||row->find(AllocationStateKey))){require(!chosen,ErrorCode::Conflict,"Ambiguous allocation intent");chosen=row;}}}
    if(!chosen){return std::nullopt;}require(file.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve inventory recovery before allocation");
    const auto row=seedRow(file.document(),hash);const auto fields=allocationFields(*row.node);require(fields.has_value(),ErrorCode::Conflict,"Missing allocation intent");
    require(!row.node->find(VerifiedSeedKey),ErrorCode::Conflict,"Allocation cannot acquire a read-only existing-data subject");
    if(fields->second!=AllocationState::Complete){require(row.saved.startup==core::StartupState::Stopped&&!row.saved.forceStart&&!row.paused,ErrorCode::Conflict,"Pending allocation requires stopped/unforced/unpaused intent");}
    const auto* snapshot=row.node->find(AllocationSnapshotKey);require(snapshot&&snapshot->type==Node::Type::Bytes&&snapshot->bytes.size()==64&&std::ranges::all_of(snapshot->bytes,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),ErrorCode::InvalidFormat,"Invalid allocation destination snapshot");
    return std::optional{AllocationIntent{hash,fields->first,fields->second,raw(file.document(),*row.node),row.paused,std::string(snapshot->bytes)}};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot read allocation intent"});}
}
std::expected<void,Error> validateAllocationTargets(const ProfileLease& lease,const AllocationIntent& intent,const core::Document& source,std::span<const fs::path> paths){
  try{
    const auto context=take(readOriginalFilenameContext(lease));const auto subject=metadataSubject(source.serialize(),context.conversions,context.decoderShowAll);
    require(subject.hash==intent.hash&&intakeDestinationSnapshot(subject,paths)==intent.targetSnapshot,ErrorCode::Conflict,"Reviewed allocation destinations changed before preparation");return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate reviewed allocation destinations"});}
}
std::expected<void,Error> publishAllocationState(const ProfileLease& lease,const AllocationIntent& intent,AllocationState phase,void(*observer)(SaveStage)){
  try{
    require(phase==AllocationState::Complete||phase==AllocationState::Cancelled,ErrorCode::InvalidFormat,"Allocation publication needs settled work");
    require(intent.state!=AllocationState::Complete,ErrorCode::Conflict,"Completed allocation has no new work authority");
    require(!hasPendingTorrentIntake(lease)&&!hasPendingFileOperation(lease)&&!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Finish profile recovery before allocation publication");
    auto file=take(ConfigFile::load(lease,"downloads.config"));require(file.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve inventory recovery before allocation publication");
    const auto current=seedRow(file.document(),intent.hash);const auto fields=allocationFields(*current.node);
    require(fields&&fields->first==intent.mode&&fields->second==intent.state&&raw(file.document(),*current.node)==intent.row&&current.paused==intent.paused
        &&current.saved.startup==core::StartupState::Stopped&&!current.saved.forceStart&&!current.paused,ErrorCode::Conflict,"Saved allocation intent changed; explicit re-review required");
    auto row=parse(raw(file.document(),*current.node));put(row,"state","i70e");put(row,"forceStart","i0e");put(row,"allocated",phase==AllocationState::Complete?"i1e":"i0e");put(row,AllocationStateKey,i(static_cast<int>(phase)));
    replaceSeedRow(file.document(),current,row.serialize());
    if(phase==AllocationState::Complete&&intent.mode==core::TorrentAllocationMode::Paused){
      const auto* pauses=file.document().find("pause_data");require(!pauses||pauses->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid original pause list");
      std::string entries=pauses?raw(file.document(),*pauses):"le";const auto key=intent.hash.bytes();
      entries.insert(entries.size()-1,"d4:hash"+b(std::string_view{reinterpret_cast<const char*>(key.data()),key.size()})+"5:forcei0e5:owneri1ee");put(file.document(),"pause_data",std::move(entries));
    }
    return file.save(lease,observer);
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Allocation publication failed; inspect recovery before retry"});}
}
