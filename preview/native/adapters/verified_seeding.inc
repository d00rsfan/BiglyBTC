// Included by torrent_intake.cpp: uses the same bounded document/error helpers.
namespace {
struct SeedRow { core::SavedDownload saved; const Node* node; bool paused; };
SeedRow seedRow(const core::Document& doc,const core::TorrentHash& hash) {
  const auto library=core::readLibrary(doc);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid verified-seeding inventory");
  const core::SavedDownload* found=nullptr;
  for(const auto& row:library->downloads){if(row.hash&&*row.hash==hash){require(!found,ErrorCode::Conflict,"Ambiguous verified-seeding identity");found=&row;}}
  require(found&&found->persistent&&!found->legacyKey,ErrorCode::Unsupported,"Verified seeding requires one persistent list-form row");
  const auto* rows=doc.find("downloads");require(rows&&rows->type==Node::Type::List&&found->sourceIndex<rows->children.size(),ErrorCode::InvalidFormat,"Missing verified-seeding row");
  const bool paused=std::ranges::any_of(library->paused,[&](const auto& pause){return pause.hash==hash;});
  return {*found,&rows->children[found->sourceIndex],paused};
}
VerifiedSeedState seedState(const Node& row) {
  const auto* marker=row.find(VerifiedSeedKey);if(!marker){return VerifiedSeedState::None;}
  require(marker->type==Node::Type::Integer&&(marker->integer==1||marker->integer==2),ErrorCode::InvalidFormat,"Invalid verified-seeding intent");
  return marker->integer==1?VerifiedSeedState::Pending:VerifiedSeedState::Released;
}
void replaceSeedRow(core::Document& doc,const SeedRow& before,std::string value) {
  const auto* rows=doc.find("downloads");std::string list="l";
  for(std::size_t n=0;n<rows->children.size();++n){list+=n==before.saved.sourceIndex?value:raw(doc,rows->children[n]);}
  put(doc,"downloads",list+'e');
}
}
std::expected<std::optional<VerifiedSeedIntent>,Error> readVerifiedSeedIntent(
    const ProfileLease& lease,const core::TorrentHash& hash) {
  try {
    auto file=take(ConfigFile::load(lease,"downloads.config"));
    // No marker: preserve Download's original legacy/recovery acceptance.
    const auto library=core::readLibrary(file.document());
    require(library.has_value(),ErrorCode::InvalidFormat,"Invalid inventory");
    const Node* selected=nullptr;
    for(const auto& saved:library->downloads){if(saved.hash&&*saved.hash==hash){
      const auto* rows=file.document().find("downloads");
      const auto* row=rows?&rows->children.at(saved.sourceIndex):file.document().find(*saved.legacyKey);
      if(row&&row->find(VerifiedSeedKey)){require(!selected,ErrorCode::Conflict,"Ambiguous verified-seeding intent");selected=row;}
    }}
    if(!selected){return std::nullopt;}
    require(file.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve inventory recovery before verified seeding");
    const auto row=seedRow(file.document(),hash);const auto state=seedState(*row.node);
    if(state==VerifiedSeedState::None){return std::nullopt;}
    return std::optional{VerifiedSeedIntent{hash,state,raw(file.document(),*row.node),row.paused,row.saved.forceStart,row.saved.startup}};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot read verified-seeding intent"});}
}
std::expected<void,Error> resetVerifiedSeedForRetry(const ProfileLease& lease,const core::TorrentHash& hash) {
  try {
    auto file=take(ConfigFile::load(lease,"downloads.config"));
    require(file.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve inventory recovery before retry");
    const auto row=seedRow(file.document(),hash);require(seedState(*row.node)!=VerifiedSeedState::None,ErrorCode::Conflict,"No verified-seeding intent");
    auto value=parse(raw(file.document(),*row.node));put(value,"state","i70e");put(value,"forceStart","i0e");put(value,VerifiedSeedKey,"i1e");
    replaceSeedRow(file.document(),row,value.serialize());
    if(const auto* pauses=file.document().find("pause_data")){
      require(pauses->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid original pause list");std::string kept="l";
      for(const auto& pause:pauses->children){
        const auto* identity=pause.type==Node::Type::Bytes?&pause:pause.find("hash");
        bool selected=false;
        if(identity&&identity->type==Node::Type::Bytes){const auto parsed=core::TorrentHash::parse(identity->bytes);selected=parsed&&*parsed==hash;}
        if(!selected){kept+=raw(file.document(),pause);}
      }put(file.document(),"pause_data",kept+'e');
    }
    const auto saved=file.save(lease);if(!saved){return std::unexpected(saved.error());}return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Verified-seeding retry save failed; inspect recovery before retry"});}
}
std::expected<void,Error> validateVerifiedSeedState(const ProfileLease& lease,const core::TorrentHash& hash,
    const OriginalTorrentIdentity& admitted,std::string_view mappingScope) {
  try{
    auto store=take(TorrentStateStore::open(lease));const auto state=take(store.loadAttributes(lease,hash));
    require(!state.recoveredBackup(),ErrorCode::Conflict,"Resolve state recovery before verified seeding");
    const auto current=metadataSubject(state.document().serialize());
    const auto relation=take(compareOriginalTorrentIdentities(current.identity,admitted));
    require(relation==OriginalIdentityRelation::SameSelected,ErrorCode::Conflict,"Verified-seeding raw/full identity changed");
    const auto scope=take(originalQueueMappingScope(state.document()));
    require(scope==mappingScope,ErrorCode::Conflict,"Verified-seeding physical mapping changed");
    const auto flags=core::originalDownloadFlags(state.document().find("attributes"));
    require(flags&&(*flags&8),ErrorCode::Conflict,"Original completion-move intent changed during verification");return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate current verified-seeding state"});}
}
std::expected<void,Error> publishVerifiedSeed(const ProfileLease& lease,const VerifiedSeedIntent& intent,
    void(*observer)(SaveStage)) {
  try {
    require(!hasPendingTorrentIntake(lease)&&!hasPendingFileOperation(lease)&&!hasPendingOriginalCompaction(lease),
        ErrorCode::Conflict,"Finish pending profile recovery before verified-seeding publication");
    auto file=take(ConfigFile::load(lease,"downloads.config"));
    require(file.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve inventory recovery before seeding");
    const auto row=seedRow(file.document(),intent.hash);const auto state=seedState(*row.node);
    require(state==intent.state&&raw(file.document(),*row.node)==intent.row&&row.paused==intent.paused,
        ErrorCode::Conflict,"Saved intent changed during verification; explicit retry required");
    require((state==VerifiedSeedState::Pending&&row.saved.startup==core::StartupState::Stopped&&!row.paused&&!row.saved.forceStart)
        ||state==VerifiedSeedState::Released,ErrorCode::Conflict,"No current verification intent");
    // Released is historical intent. Preserve later original/manual force,
    // Pause/Stop and queue choices exactly after the fresh RO proof.
    if(state==VerifiedSeedState::Released){return {};}
    auto value=parse(raw(file.document(),*row.node));put(value,"state","i75e");put(value,"forceStart","i0e");put(value,VerifiedSeedKey,"i2e");
    if(value.serialize()==raw(file.document(),*row.node)){return {};}
    replaceSeedRow(file.document(),row,value.serialize());
    const auto saved=file.save(lease,observer);if(!saved){return std::unexpected(saved.error());}return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Verified-seeding publication is uncertain; explicit recovery required"});}
}
