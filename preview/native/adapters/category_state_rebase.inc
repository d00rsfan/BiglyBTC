// Refresh one known attribute of an already prepared owner from the current
// profile state. Keep its buffered resume, file geometry and other metadata.
namespace {
bool categoryValueEqual(const core::Node& a,const core::Node& b){
  if(a.type!=b.type){return false;}
  if(a.type==core::Node::Type::Integer){return a.integer==b.integer;}
  if(a.type==core::Node::Type::Bytes){return a.bytes==b.bytes;}
  if(a.children.size()!=b.children.size()){return false;}
  if(a.type==core::Node::Type::List){
    for(std::size_t i=0;i<a.children.size();++i){if(!categoryValueEqual(a.children[i],b.children[i])){return false;}}
    return true;
  }
  std::vector<std::size_t> x,y;
  for(std::size_t i=0;i<a.children.size();i+=2){x.push_back(i);y.push_back(i);}
  std::sort(x.begin(),x.end(),[&](auto i,auto j){return a.children[i].bytes<a.children[j].bytes;});
  std::sort(y.begin(),y.end(),[&](auto i,auto j){return b.children[i].bytes<b.children[j].bytes;});
  for(std::size_t i=0;i<x.size();++i){
    if(a.children[x[i]].bytes!=b.children[y[i]].bytes||!categoryValueEqual(a.children[x[i]+1],b.children[y[i]+1])){return false;}
  }
  return true;
}
bool categoryOptionalEqual(const core::Node* a,const core::Node* b){return (!a&&!b)||(a&&b&&categoryValueEqual(*a,*b));}
bool selectedAttributeOnlyChange(const core::Document& a,const core::Document& b,std::string_view field){
  const auto& x=a.root();const auto& y=b.root();
  if(x.type!=core::Node::Type::Dictionary||y.type!=x.type||x.children.size()!=y.children.size()){return false;}
  for(std::size_t i=0;i<x.children.size();i+=2){
    if(x.children[i].bytes!=y.children[i].bytes){return false;}
    const auto& before=x.children[i+1];const auto& after=y.children[i+1];
    if(x.children[i].bytes=="azureus_properties"||x.children[i].bytes=="azureus_private_properties"){
      // Cache hydration may reorder these dictionaries, but no plugin field,
      // value or type may change (including ratings and network permissions).
      if(before.type!=core::Node::Type::Dictionary||!categoryValueEqual(before,after)){return false;}
      continue;
    }
    if(x.children[i].bytes!="attributes"){
      // Info (and therefore full v1/v2 identity), resume,
      // layout/network authority and every other root range stay byte-exact.
      if(a.originalBytes().substr(before.begin,before.end-before.begin)!=b.originalBytes().substr(after.begin,after.end-after.begin)){return false;}
      continue;
    }
    if(before.type!=core::Node::Type::Dictionary||after.type!=before.type){return false;}
    std::size_t count=0;
    for(std::size_t j=0;j<before.children.size();j+=2){
      const auto key=before.children[j].bytes;if(key==field){continue;}++count;
      const auto* actual=after.find(key);if(!actual||!categoryValueEqual(before.children[j+1],*actual)){return false;}
    }
    std::size_t actual=0;for(std::size_t j=0;j<after.children.size();j+=2){if(after.children[j].bytes!=field){++actual;}}
    if(count!=actual){return false;}
  }
  return true;
}
struct CategoryPhysicalInput {
  StateDescriptor fd;
  std::optional<StateStamp> stamp;
};

std::expected<std::optional<std::string>,Error> categoryPhysicalBytes(const CategoryPhysicalInput& input) {
  if (!input.stamp) { return std::nullopt; }
  if (input.stamp->size < 0 || static_cast<std::uint64_t>(input.stamp->size) > compatibleConfigLimits().inputBytes) {
    return error(ErrorCode::InvalidFormat, "Category physical source exceeds its limit");
  }
  std::string bytes(static_cast<std::size_t>(input.stamp->size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    const auto count = readStateAt(input.fd.get(), bytes.data() + offset, bytes.size() - offset, offset);
    if (count <= 0) { return error(ErrorCode::Io, "Cannot read retained category source"); }
    offset += static_cast<std::size_t>(count);
  }
  const auto after = stateStamp(input.fd.get());
  if (!after || !sameStateSnapshot(*after, *input.stamp)) {
    return error(ErrorCode::Conflict, "Retained category source changed during read");
  }
  return std::optional{std::move(bytes)};
}

std::expected<CategoryPhysicalInput,Error> categoryPhysical(StateHandle directory, const std::string& name) {
  const auto named = stateStampAt(directory, name);
  if (!named) { return std::unexpected(named.error()); }
  if (!*named) { return CategoryPhysicalInput{}; }
  if (!(*named)->regular || (*named)->links != 1 || !(*named)->owned) {
    return error(ErrorCode::UnsafePath, "Category rebase source must be owned regular single-link data");
  }
  auto fd = openStateFile(directory, name);
  if (!fd) { return error(ErrorCode::Conflict, "Category rebase source changed while opening"); }
  const auto held = stateStamp(fd->get());
  if (!held || !sameStateSnapshot(**named, *held)) {
    return error(ErrorCode::Conflict, "Category rebase source changed while opening");
  }
  return CategoryPhysicalInput{std::move(*fd), *held};
}
} // namespace

std::expected<std::string,Error> TorrentStateStore::rebaseCategory(
    const ProfileLease& lease,TorrentStateFile& state)const {
  return rebaseSelectedAttribute(lease,state,"category");
}
std::expected<std::string,Error> TorrentStateStore::rebaseSwarmTags(
    const ProfileLease& lease,TorrentStateFile& state)const {
  return rebaseSelectedAttribute(lease,state,"stag");
}
std::expected<std::string,Error> TorrentStateStore::rebaseSelectedAttribute(
    const ProfileLease& lease,TorrentStateFile& state,std::string_view field)const {
  try{
    auto gate=lease.sourceAccess();if(!gate){return std::unexpected(gate.error());}
    auto valid=validate(lease);if(!valid){return std::unexpected(valid.error());}
    auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
    if(!state.prepared_||state.attributesOnly_||state.backup_||!state.original_
        ||state.device_!=activeDevice_||state.inode_!=activeInode_){return error(ErrorCode::Conflict,"Original prepared state required for category rebase");}
    const auto file=state.hash_.hex()+".dat";
    const std::array<std::string,8> names{file,file+".bak",file+".saving",file+".bak.saving","cache.dat","cache.dat.bak","cache.dat.saving","cache.dat.bak.saving"};
    std::array<CategoryPhysicalInput,8> inputs;
    for(std::size_t i=0;i<names.size();++i){auto input=categoryPhysical(active_.get(),names[i]);if(!input){return std::unexpected(input.error());}inputs[i]=std::move(*input);}
    if(!inputs[0].stamp||inputs[2].stamp||inputs[3].stamp||inputs[6].stamp||inputs[7].stamp){return error(ErrorCode::Conflict,"Category state recovery candidates require explicit review");}
    auto primaryBytes=categoryPhysicalBytes(inputs[0]);if(!primaryBytes){return std::unexpected(primaryBytes.error());}
    auto cacheBytes=categoryPhysicalBytes(inputs[4]);if(!cacheBytes){return std::unexpected(cacheBytes.error());}
    auto original=EncodedDocument::parse(*state.original_,compatibleConfigLimits());if(!original){return error(ErrorCode::InvalidFormat,"Invalid prepared physical category preimage");}
    auto fresh=loadAttributes(lease,state.hash_);if(!fresh){return std::unexpected(fresh.error());}
    if(fresh->backup_||!fresh->original_){return error(ErrorCode::Conflict,"Category rebase requires actual current primary state");}
    if(fresh->original_!=*primaryBytes||fresh->attributeCache_!=*cacheBytes){return error(ErrorCode::Conflict,"Category path reads differ from retained source descriptors");}
    // A completed attribute writer retires its selected cache entry. Refuse a
    // later/foreign selected entry rather than let it mask an owner's save.
    if(fresh->attributeCache_){
      auto cache=EncodedDocument::parse(*fresh->attributeCache_,compatibleConfigLimits());if(!cache){return error(ErrorCode::InvalidFormat,"Invalid category rebase cache");}
      if(const auto* entries=cache->document().find("state")){
        if(entries->type!=core::Node::Type::List){return error(ErrorCode::InvalidFormat,"Invalid category rebase cache list");}
        const auto hash=state.hash_.bytes();const std::string_view key(reinterpret_cast<const char*>(hash.data()),hash.size());
        for(const auto& entry:entries->children){const auto* id=entry.find("hash");if(id&&id->type==core::Node::Type::Bytes&&id->bytes==key){return error(ErrorCode::Conflict,"Selected state cache still masks category publication");}}
      }
    }
    auto actual=core::Document::parse(fresh->document().serialize(),compatibleConfigLimits());
    if(!actual||!selectedAttributeOnlyChange(original->document(),*actual,field)){
      return error(ErrorCode::Conflict,"Saved state changed beyond the selected metadata field");
    }
    const auto* before=original->document().find("attributes");const auto* after=actual->find("attributes");
    const auto* pending=state.document().find("attributes");
    if(!before||!after||!pending||pending->type!=core::Node::Type::Dictionary
        ||!categoryOptionalEqual(before->find(field),pending->find(field))){
      return error(ErrorCode::Conflict,"Buffered metadata differs from its physical preimage");
    }
    const auto* value=after->find(field);
    if (value && field=="category" && (value->type!=core::Node::Type::Bytes || value->bytes.size()>4096)) {
      return error(ErrorCode::InvalidFormat,"Invalid committed category metadata");
    }
    if (value && field=="stag" && (value->type!=core::Node::Type::List ||
        value->children.size()>128 || !std::ranges::all_of(value->children,[](const auto& name) {
          return name.type==core::Node::Type::Bytes && name.bytes.size()<=128;
        }))) {
      return error(ErrorCode::InvalidFormat,"Invalid committed Swarm Tag list");
    }
    std::string attributes="d";
    for(std::size_t i=0;i<pending->children.size();i+=2){if(pending->children[i].bytes==field){continue;}
      for(const auto* item:{&pending->children[i],&pending->children[i+1]}){auto encoded=core::encodeValue(*item,compatibleConfigLimits());if(!encoded){return error(ErrorCode::InvalidFormat,"Cannot retain buffered attributes");}attributes+=*encoded;}
    }
    if (value) {
      auto encoded=core::encodeValue(*value,compatibleConfigLimits());
      if (!encoded) { return error(ErrorCode::InvalidFormat,"Cannot encode committed attributes"); }
      attributes+=std::to_string(field.size())+':'+std::string(field)+*encoded;
    }
    attributes+='e';
    auto changed=state.document_;if(!changed.replaceValue("attributes",std::move(attributes))){return error(ErrorCode::InvalidFormat,"Buffered state exceeds category rebase limits");}
    // All allocating work precedes mutation. Keep prepared flag, hash and every
    // locally buffered resume/property edit; only CAS bytes/category advance.
    auto physical=fresh->original_;auto cache=fresh->attributeCache_;auto snapshot=actual->serialize();
    for(std::size_t i=0;i<names.size();++i){auto current=categoryPhysical(active_.get(),names[i]);if(!current){return std::unexpected(current.error());}
      if(bool(current->stamp)!=bool(inputs[i].stamp)){return error(ErrorCode::Conflict,"Category source names changed before rebase");}
      if (inputs[i].stamp) {
        const auto held = stateStamp(inputs[i].fd.get());
        if (!held || !sameStateSnapshot(*held, *inputs[i].stamp)
            || !sameStateSnapshot(*current->stamp, *inputs[i].stamp)) {
          return error(ErrorCode::Conflict, "Category source identity changed before rebase");
        }
      }
    }
    valid=validate(lease);if(!valid){return std::unexpected(valid.error());}
    state.document_=std::move(changed);state.original_=std::move(physical);state.attributeCache_=std::move(cache);
    return std::expected<std::string,Error>{std::in_place,std::move(snapshot)};
  }catch(const std::bad_alloc&){return error(ErrorCode::Io,"Insufficient memory for category-only state rebase");}
}
