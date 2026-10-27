// Private helpers within torrent_intake.cpp; no new IO owner or transaction.
struct HybridAliasSource {
  core::TorrentHash hash;
  std::string state;
  std::optional<std::string> cache;
  std::uint64_t stateDevice=0,stateInode=0,cacheDevice=0,cacheInode=0;
};
std::string hashBytes(const core::TorrentHash& value) {
  const auto bytes=value.bytes();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
core::Document withoutKey(const core::Document& document,std::string_view omit) {
  require(document.root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Hybrid clone requires dictionary");
  std::string result="d";
  for(std::size_t n=0;n<document.root().children.size();n+=2){
    const auto& key=document.root().children[n];if(key.bytes==omit){continue;}
    result+=raw(document,key);result+=raw(document,document.root().children[n+1]);
  }
  return parse(result+'e',MaximumInventory);
}
core::Document hybridSourceDocument(const HybridAliasSource& source) {
  auto decoded=EncodedDocument::parse(source.state,compatibleConfigLimits());
  require(decoded.has_value(),ErrorCode::InvalidFormat,"Invalid original hybrid source state");
  auto document=decoded->document();
  if(source.cache){
    auto cache=EncodedDocument::parse(*source.cache,compatibleConfigLimits());
    require(cache.has_value(),ErrorCode::InvalidFormat,"Invalid original hybrid source cache");
    require(cache->compressed(),ErrorCode::InvalidFormat,"Hybrid alias needs original compressed cache");
    const auto* entries=cache->document().find("state");
    if(entries){
      require(entries->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid original hybrid cache");
      const Node* chosen=nullptr;
      for(const auto& entry:entries->children){
        const auto* hash=entry.find("hash");
        if(hash&&hash->type==Node::Type::Bytes&&hash->bytes==hashBytes(source.hash)){chosen=&entry;}
      }
      if(chosen){
        for(auto [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){
          if(const auto* value=chosen->find(from)){
            require(value->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid cached hybrid properties");
            put(document,to,raw(cache->document(),*value));
          }
        }
      }
    }
  }
  return parse(document.serialize(),MaximumMetadata);
}
void validateHybridAlias(const HybridAliasSource& source,const MetadataSubject& target,
    const core::Document& inventory,const std::string& destination,std::optional<std::string_view> conversions={},bool decoderShowAll=false) {
  const auto old=metadataSubject(hybridSourceDocument(source).serialize(),conversions,decoderShowAll);
  require(old.identity.full.v1&&old.identity.full.v2&&source.hash==old.identity.originalKey
      &&take(compareOriginalTorrentIdentities(old.identity,target.identity))==OriginalIdentityRelation::AlternateSelected,
      ErrorCode::Conflict,"Alias must select the other identity of exactly one full hybrid");
  if(target.identity.selected==OriginalSelectedHash::V2){
    // cb8 setV2FileHashes dereferences an absent tree for every real empty
    // hybrid file. Never publish an original-unloadable selected-V2 companion.
    require(std::ranges::none_of(old.layout.files,[](const auto& file){return !file.padding&&file.bytes==0;}),
        ErrorCode::Unsupported,"Original hybrid v2 cannot safely reload real empty files");
  }
  require(take(originalTorrentInfoSubject(old.torrent))==take(originalTorrentInfoSubject(target.torrent)),
      ErrorCode::Conflict,"Hybrid alias changed exact raw info bytes");
  const auto library=core::readLibrary(inventory);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid hybrid source inventory");
  const core::SavedDownload* selected=nullptr;
  for(const auto& row:library->downloads){if(row.hash&&*row.hash==source.hash){
    require(!selected,ErrorCode::Conflict,"Duplicate hybrid source key");selected=&row;
  }}
  require(selected&&selected->persistent&&selected->startup==core::StartupState::Stopped
      &&!selected->forceStart&&!selected->legacySavePath&&selected->saveFile
      &&*selected->saveFile==old.layout.name&&selected->saveDirectory==destination,
      ErrorCode::Unsupported,"Hybrid alias requires stopped modern save root without force");
  const auto* attributes=old.torrent.find("attributes");
  require(attributes&&attributes->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Hybrid source attributes unavailable");
  const auto* stores=attributes->find("storetypes");
  require(stores&&stores->type==Node::Type::List&&stores->children.size()==old.layout.files.size(),
      ErrorCode::Unsupported,"Hybrid alias requires explicit original linear types");
  // Read the exact selected row, not a reconstructed inventory/priority guess.
  const auto* rows=inventory.find("downloads");
  require(rows&&rows->type==Node::Type::List,ErrorCode::Unsupported,"Hybrid aliases require list inventory");
  const auto& selectedRow=rows->children.at(selected->sourceIndex);
  const auto policies=compat::originalFilePolicy(selectedRow,old.layout.files.size());
  require(policies.has_value(),ErrorCode::InvalidFormat,"Invalid hybrid source file choices");
  std::vector<std::string> bases;
  for(std::size_t n=0;n<old.layout.files.size();++n){
    const auto& file=old.layout.files[n];const auto& kind=stores->children[n];
    require(file.padding||(kind.type==Node::Type::Bytes&&kind.bytes=="L"&&!(*policies)[n].skipped),
        ErrorCode::Unsupported,"Hybrid alias currently requires selected linear real files");
    bases.push_back((fs::path(destination)/(old.layout.multifile?fs::path(old.layout.name)/file.path:fs::path(old.layout.name))).native());
  }
  const auto links=compat::resolveOriginalFileLinks(attributes->find("filelinks2"),bases);
  require(links&&links->diagnostics.empty(),ErrorCode::Unsupported,"Unresolved hybrid source links cannot authorize sharing");
  for(std::size_t n=0;n<old.layout.files.size();++n){
    require(old.layout.files[n].padding||!links->byIndex[n],ErrorCode::Unsupported,"Linked hybrid payload cannot be shared");
  }
  for(const auto* key:{"BiglyBTC.completion","BiglyBTC.compactConversion"}){
    if(const auto* pending=attributes->find(key)){
      require(pending->type==Node::Type::Dictionary&&pending->children.empty(),ErrorCode::Conflict,"Settle original storage recovery before alias creation");
    }
  }
}
void hybridSourceCurrent(int root,const HybridAliasSource& source,std::optional<std::string_view> conversions={},bool decoderShowAll=false) {
  auto active=childDirectory(root,"active",false);
  require(active.get()>=0,ErrorCode::Conflict,"Hybrid source state directory disappeared");
  const auto current=read(active.get(),source.hash.hex()+".dat",MaximumMetadata);
  require(current&&current->bytes==source.state&&current->info.st_dev==source.stateDevice&&current->info.st_ino==source.stateInode,
      ErrorCode::Conflict,"Hybrid source state changed before publication");
  const auto cache=read(active.get(),"cache.dat",MaximumInventory);
  require((cache?std::optional{cache->bytes}:std::nullopt)==source.cache
      &&(!cache||(cache->info.st_dev==source.cacheDevice&&cache->info.st_ino==source.cacheInode)),
      ErrorCode::Conflict,"Hybrid source cache changed before publication");
  auto controls=childDirectory(active.get(),source.hash.hex().c_str(),false);
  if(controls.get()>=0){
    const auto old=metadataSubject(hybridSourceDocument(source).serialize(),conversions,decoderShowAll);
    for(std::size_t n=0;n<old.layout.files.size();++n){if(!old.layout.files[n].padding){
      for(const auto* marker:{".dat",".dat.2"}){
        const auto leaf="fmfile"+std::to_string(n)+marker;
        for(const auto* suffix:{"",".bak",".saving",".bak.saving"}){absent(controls.get(),leaf+suffix);}
      }
    }}
    childIdentity(active.get(),source.hash.hex().c_str(),controls.get());
  }
  childIdentity(root,"active",active.get());
}
