// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside storage, after the existing owning CategoryStore::Impl.
namespace {
using fileop::require;
template<class T>T cdTake(std::expected<T,Error> value){if(!value){throw fileop::Failure{value.error()};}return std::move(*value);}
void cdTake(std::expected<void,Error> value){if(!value){throw fileop::Failure{value.error()};}}
std::string cdEncoded(std::expected<std::string,CodecError> value){require(bool(value),ErrorCode::InvalidFormat,"Cannot encode deletion postimage");return std::move(*value);}
constexpr std::size_t CdDocument=4*1024*1024,CdTotal=32*1024*1024,CdJournal=16*1024*1024,CdDownloads=128;
core::Limits cdLimits(){return {.inputBytes=CdJournal,.stringBytes=CdJournal,.nodes=100000,.depth=64};}
std::string cdBytes(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
std::string cdNumber(std::int64_t n){return 'i'+std::to_string(n)+'e';}
std::string cdList(const std::vector<std::string>& values){std::string out="l";for(const auto& v:values){out+=v;}return out+'e';}
std::string cdDict(const std::map<std::string,std::string>& values){std::string out="d";for(const auto& [k,v]:values){out+=cdBytes(k)+v;}return out+'e';}
core::TorrentHash cdMemberHash(std::string_view text){
  require(text.size()==40||text.size()==64,ErrorCode::InvalidFormat,"Invalid pending deletion identity length");
  constexpr std::string_view digits="0123456789ABCDEF";std::string raw(text.size()/2,'\0');
  for(std::size_t i=0;i<raw.size();++i){const auto hi=digits.find(text[2*i]),lo=digits.find(text[2*i+1]);
    require(hi!=std::string_view::npos&&lo!=std::string_view::npos,ErrorCode::InvalidFormat,"Invalid canonical pending deletion identity");
    raw[i]=static_cast<char>((hi<<4)|lo);
  }
  auto hash=core::TorrentHash::parse(raw);require(hash&&hash->hex()==text,ErrorCode::InvalidFormat,"Invalid pending deletion identity");return *hash;
}
std::vector<std::int64_t> cdDirectory(StateHandle directory){
  const auto s=cdTake(stateStamp(directory));
  require(s.directory&&s.owned,ErrorCode::UnsafePath,"Deletion directory must be owned");
  require(s.device<=INT64_MAX&&s.inode<=INT64_MAX,ErrorCode::Unsupported,"Deletion directory identity exceeds journal range");
  return {static_cast<std::int64_t>(s.device),static_cast<std::int64_t>(s.inode),
      s.mode,static_cast<std::int64_t>(s.owner),static_cast<std::int64_t>(s.group)};
}
std::vector<std::int64_t> cdStamp(const StateStamp& s){
  require(s.device<=INT64_MAX&&s.inode<=INT64_MAX,ErrorCode::Unsupported,"Deletion input identity exceeds journal range");
  return {static_cast<std::int64_t>(s.device),static_cast<std::int64_t>(s.inode),s.mode,
      static_cast<std::int64_t>(s.owner),static_cast<std::int64_t>(s.group),static_cast<std::int64_t>(s.links),
      s.size,s.modified.tv_sec,s.modified.tv_nsec,s.changed.tv_sec,s.changed.tv_nsec};
}
std::string cdKey(int area,const std::string& name){return std::to_string(area)+':'+name;}
struct CdGuard {int area=0;std::string name;bool present=false;std::vector<std::int64_t> stamp;std::string digest;bool operator==(const CdGuard&)const=default;};
struct CdCapture {StateDescriptor active;std::vector<std::int64_t> rootId,activeId;std::map<std::string,CdGuard> guards;std::map<std::string,std::string> bytes;};
std::vector<core::TorrentHash> cdInventory(std::string_view raw){
  require(raw.size()<=CdDocument,ErrorCode::Unsupported,"Deletion inventory exceeds bound");auto d=core::Document::parse(std::string(raw));require(bool(d),ErrorCode::InvalidFormat,"Invalid deletion inventory");auto lib=core::readLibrary(*d,{.downloads=CdDownloads,.paused=CdDownloads});
  require(lib&&!lib->legacyMap&&lib->issues.empty(),ErrorCode::Conflict,"Complete unambiguous downloads-list inventory required");
  std::map<std::string,core::TorrentHash> hashes;for(const auto& row:lib->downloads){require(row.hash&&row.persistent,ErrorCode::Conflict,"Every deletion inventory row needs a persistent identity");require(hashes.emplace(row.hash->hex(),*row.hash).second,ErrorCode::Conflict,"Duplicate deletion inventory identity");}
  std::vector<core::TorrentHash> out;for(const auto& [key,h]:hashes){static_cast<void>(key);out.push_back(h);}return out;
}
CdCapture cdCapture(const ProfileLease& lease,const std::vector<core::TorrentHash>& hashes,int version){
  if (version==1) {
    // The old journal cannot attest any present tag metadata or candidates.
    for (const auto& name : CategoryTagNames) {
      require(freshStamp(lease.directoryHandle(),name,{}),
          ErrorCode::Conflict,"Legacy deletion with saved category tag metadata requires preserved review");
    }
  }
  CdCapture out;out.rootId=cdDirectory(lease.directoryHandle());
  out.active=cdTake(detail::openStateDirectory(lease.directoryHandle(),"active"));
  const auto active=cdTake(stateStamp(out.active.get()));
  out.activeId=cdDirectory(out.active.get());
  struct Held {int area;std::string name;Input value;};std::vector<Held> heldFiles;std::size_t total=0;
  const auto add=[&](int area,const std::string& name){auto value=cdTake(capture(area?out.active.get():lease.directoryHandle(),name,true,CdDocument));CdGuard g{area,name,bool(value.stamp),{}, {}};
    if(value.stamp){g.stamp=cdStamp(*value.stamp);require(value.bytes&&value.bytes->size()<=CdTotal-total,ErrorCode::Unsupported,"Deletion input set exceeds bound");total+=value.bytes->size();g.digest=fileop::digest(*value.bytes);out.bytes.emplace(cdKey(area,name),*value.bytes);}require(out.guards.emplace(cdKey(area,name),std::move(g)).second,ErrorCode::InvalidFormat,"Duplicate deletion guard");heldFiles.push_back({area,name,std::move(value)});};
  for(const auto& n:CategoryNames){add(0,n);}for(const auto& n:InventoryNames){add(0,n);}for(const auto* n:{"cache.dat","cache.dat.bak","cache.dat.saving","cache.dat.bak.saving"}){add(1,n);}
  if (version==2) { for (const auto& name : CategoryTagNames) { add(0,name); } }
  for(const auto& h:hashes){const auto names=targetNames(h);for(unsigned i=0;i<4;++i){add(1,names[i]);}}
  for(const auto& item:heldFiles){
    const auto parent=item.area?out.active.get():lease.directoryHandle();
    require((!item.value.witnessRequired||item.value.stillHeld())&&freshStamp(parent,item.name,item.value.stamp),
        ErrorCode::Conflict,"Deletion inputs changed during complete capture");
  }
  require(activeNamed(lease.directoryHandle(),out.active.get(),active),ErrorCode::Conflict,"Deletion active directory changed");
  return out;
}
struct CdPlan {
  std::string name,definitionsBefore,definitionsAfter,inventory,previous;
  std::vector<std::string> members;
  std::size_t done=0;
  bool published=false;
  int version=2;
  std::int32_t categoryTagId=0;
  std::vector<std::int64_t> rootId,activeId;
  std::map<std::string,CdGuard> guards;
};
std::string cdNumbers(const std::vector<std::int64_t>& v){std::vector<std::string> a;for(auto n:v){a.push_back(cdNumber(n));}return cdList(a);}
std::string cdState(const CdPlan& p){std::vector<std::string> members,guards;for(const auto& h:p.members){members.push_back(cdBytes(h));}for(const auto& [key,g]:p.guards){static_cast<void>(key);guards.push_back(cdDict({{"area",cdNumber(g.area)},{"digest",cdBytes(g.digest)},{"name",cdBytes(g.name)},{"present",cdNumber(g.present)},{"stamp",cdNumbers(g.stamp)}}));}
  std::map<std::string,std::string> fields{{"active",cdNumbers(p.activeId)},{"definitions_after",cdBytes(p.definitionsAfter)},{"definitions_before",cdBytes(p.definitionsBefore)},{"done",cdNumber(static_cast<std::int64_t>(p.done))},{"guards",cdList(guards)},{"inventory",cdBytes(p.inventory)},{"members",cdList(members)},{"name",cdBytes(p.name)},{"previous",cdBytes(p.previous)},{"published",cdNumber(p.published)},{"root",cdNumbers(p.rootId)},{"version",cdNumber(p.version)}};
  if (p.version==2) { fields.emplace("category_tag_id",cdNumber(p.categoryTagId)); }
  return cdDict(fields);
}
const core::Node& cdField(const core::Node& n,std::string_view key,core::Node::Type type){const auto* v=n.find(key);require(v&&v->type==type,ErrorCode::InvalidFormat,"Invalid category deletion journal field");return *v;}
std::string cdString(const core::Node& n,std::string_view key){return std::string(cdField(n,key,core::Node::Type::Bytes).bytes);}
std::int64_t cdInt(const core::Node& n,std::string_view key){return cdField(n,key,core::Node::Type::Integer).integer;}
std::vector<std::int64_t> cdNumbers(const core::Node& n){require(n.type==core::Node::Type::List,ErrorCode::InvalidFormat,"Invalid deletion identity list");std::vector<std::int64_t> out;for(const auto& v:n.children){require(v.type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid deletion identity value");out.push_back(v.integer);}return out;}
CdPlan cdParse(const core::Document& d){
  require(d.root().children.size()==2,ErrorCode::InvalidFormat,"Unknown deletion journal root fields");const auto& n=cdField(d.root(),"state",core::Node::Type::Dictionary);const auto version=cdInt(n,"version");
  require((version==1&&n.children.size()==24)||(version==2&&n.children.size()==26),
      ErrorCode::InvalidFormat,"Unknown deletion journal version/fields");
  CdPlan p;
  p.version=static_cast<int>(version);
  if (version==2) {
    const auto id=cdInt(n,"category_tag_id");
    require(id==0||(id>=3&&id<=std::numeric_limits<std::int32_t>::max()),
        ErrorCode::InvalidFormat,"Invalid selected category tag ID");
    p.categoryTagId=static_cast<std::int32_t>(id);
  }
  p.name=cdString(n,"name");p.definitionsBefore=cdString(n,"definitions_before");p.definitionsAfter=cdString(n,"definitions_after");p.inventory=cdString(n,"inventory");p.previous=cdString(n,"previous");require(p.previous.empty()||p.previous.size()==64,ErrorCode::InvalidFormat,"Invalid prior journal digest");
  p.rootId=cdNumbers(cdField(n,"root",core::Node::Type::List));p.activeId=cdNumbers(cdField(n,"active",core::Node::Type::List));require(p.rootId.size()==5&&p.activeId.size()==5,ErrorCode::InvalidFormat,"Invalid journal directories");
  for(const auto& v:cdField(n,"members",core::Node::Type::List).children){require(v.type==core::Node::Type::Bytes&&p.members.size()<CdDownloads,ErrorCode::InvalidFormat,"Invalid deletion members");p.members.emplace_back(v.bytes);}
  const auto done=cdInt(n,"done"),published=cdInt(n,"published");require(done>=0&&static_cast<std::uint64_t>(done)<=p.members.size()&&(published==0||published==1),ErrorCode::InvalidFormat,"Invalid deletion progress");p.done=static_cast<std::size_t>(done);p.published=published==1;require(!p.published||p.done==p.members.size(),ErrorCode::InvalidFormat,"Premature definition deletion");
  for(const auto& v:cdField(n,"guards",core::Node::Type::List).children){require(v.type==core::Node::Type::Dictionary&&v.children.size()==10&&p.guards.size()<4*CdDownloads+11+(p.version==2?CategoryTagNames.size():0),ErrorCode::InvalidFormat,"Invalid deletion guards");const auto area=cdInt(v,"area"),present=cdInt(v,"present");require((area==0||area==1)&&(present==0||present==1),ErrorCode::InvalidFormat,"Invalid deletion guard flags");CdGuard g{static_cast<int>(area),cdString(v,"name"),present==1,cdNumbers(cdField(v,"stamp",core::Node::Type::List)),cdString(v,"digest")};require(!g.name.empty()&&g.name.size()<128&&g.name.find('/')==std::string::npos&&g.name.find('\0')==std::string::npos&&(g.present?(g.stamp.size()==11&&g.digest.size()==64):(g.stamp.empty()&&g.digest.empty())),ErrorCode::InvalidFormat,"Invalid deletion guard identity");require(p.guards.emplace(cdKey(g.area,g.name),std::move(g)).second,ErrorCode::InvalidFormat,"Duplicate deletion guard");}
  auto edit=core::editSavedCategories(p.definitionsBefore,core::SavedCategoryDelete{p.name});require(edit&&edit->changed&&edit->clearResolvedAssignments&&edit->value.source->originalBytes()==p.definitionsAfter,ErrorCode::InvalidFormat,"Deletion journal is not the exact original category edit");
  const auto hashes=cdInventory(p.inventory);std::set<std::string> all,seen;for(const auto& h:hashes){all.insert(h.hex());}for(const auto& h:p.members){require(all.contains(h)&&seen.insert(h).second,ErrorCode::InvalidFormat,"Deletion member is not unique current inventory");}return p;
}
struct CdJournalFile {CdPlan plan;ConfigFile file;std::array<Input,3> inputs;};
std::array<Input,3> cdJournalInputs(StateHandle root){std::array<Input,3> out;for(std::size_t i=0;i<3;++i){out[i]=cdTake(capture(root,CategoryJournalNames[i],true,CdJournal));}return out;}
CdJournalFile cdReadJournal(const ProfileLease& lease){
  auto before=cdJournalInputs(lease.directoryHandle());
  require(before[0].bytes&&!before[2].stamp,ErrorCode::Conflict,"Incomplete category journal requires preserved review");
  for(std::size_t i=0;i<before.size();++i){
    if(!before[i].stamp){continue;}
#ifdef _WIN32
    const auto witness=cdTake(detail::openStateWitness(lease.directoryHandle(),CategoryJournalNames[i]));
    require(sameStateSnapshot(cdTake(stateStamp(witness.get())),*before[i].stamp)&&fileop::privateEntry(witness.get()),
        ErrorCode::Conflict,"Changed journal permissions require preserved review");
#else
    require((before[i].stamp->mode&0777)==0600,ErrorCode::Conflict,"Changed journal mode requires preserved review");
#endif
  }
  auto file=cdTake(ConfigFile::load(lease,CategoryJournalNames[0],cdLimits()));require(file.recoverySource()==RecoverySource::Primary&&file.document().originalBytes()==*before[0].bytes,ErrorCode::Conflict,"Journal recovery must not substitute a companion");auto plan=cdParse(file.document());if(!plan.previous.empty()){require((plan.published&&!before[1].stamp)||(before[1].bytes&&fileop::digest(*before[1].bytes)==plan.previous),ErrorCode::Conflict,"Journal acknowledgement backup differs; review required");}else{require(!before[1].stamp,ErrorCode::Conflict,"Unexpected journal backup requires review");}auto after=cdJournalInputs(lease.directoryHandle());require(equal(before,after),ErrorCode::Conflict,"Category journal changed while reading");return {std::move(plan),std::move(file),std::move(before)};}
void cdCheckpoint(const ProfileLease& lease,CdJournalFile& journal,CdPlan plan){auto before=cdJournalInputs(lease.directoryHandle());require(equal(journal.inputs,before),ErrorCode::Conflict,"Category journal changed before acknowledgement");plan.previous=fileop::digest(*journal.inputs[0].bytes);const auto state=cdState(plan);require(state.size()<=CdJournal-16&&journal.file.document().replaceValue("state",state).has_value(),ErrorCode::Unsupported,"Deletion journal exceeds bound");cdTake(journal.file.save(lease));auto next=cdReadJournal(lease);require(cdState(next.plan)==cdState(plan),ErrorCode::Conflict,"Deletion acknowledgement was not verified");journal=std::move(next);}
std::optional<std::string> cdTagRetirement(const CdPlan& plan,const CdCapture& source){
  if (plan.version==1) { return {}; }
  const auto key=[](std::size_t index){return cdKey(0,CategoryTagNames[index]);};
  const auto primary=source.bytes.find(key(0));
  require(!source.guards.at(key(2)).present&&!source.guards.at(key(3)).present
      &&(primary!=source.bytes.end()||!source.guards.at(key(1)).present),
      ErrorCode::Conflict,"Resolve saved tag recovery candidates before category retirement");
  if (primary==source.bytes.end()) { return {}; }
  require(plan.categoryTagId>=3,ErrorCode::Conflict,"The live category tag identity is no longer known");
  auto updated=core::retireSavedCategoryTagAttributes(primary->second,plan.categoryTagId);
  if (!updated) { throw fileop::Failure{projectionError(updated.error())}; }
  return std::move(*updated);
}
void cdVerify(const CdPlan& plan,const CdCapture& now){require(plan.rootId==now.rootId&&plan.activeId==now.activeId&&plan.guards==now.guards,ErrorCode::Conflict,"Unacknowledged category publication or external edit: preserve journal for review");require(now.bytes.at(cdKey(0,"downloads.config"))==plan.inventory,ErrorCode::Conflict,"Deletion inventory changed");require(now.bytes.at(cdKey(0,"categories.config"))==(plan.published?plan.definitionsAfter:plan.definitionsBefore),ErrorCode::Conflict,"Category definition changed");}
// Expected exact effects of the EXISTING resilient attributes writer. No file
// mutation is added to that primitive; every other source guard stays exact.
void cdBackupRecheck(EncodedDocument& envelope){
  // Value-only mirror of torrent_state.cpp::recheck. saveInternal constructs
  // this backup from the NEW logical document, never the old physical primary.
  const auto limits=compatibleConfigLimits();
  const auto encode=[&](const core::Node* node){if(!node){return std::string("de");}auto bytes=core::encodeValue(*node,limits);require(bool(bytes),ErrorCode::InvalidFormat,"Cannot encode deletion backup section");return std::move(*bytes);};
  auto resume=core::Document::parse(encode(envelope.document().find("resume")),limits);require(resume&&resume->root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid deletion backup resume");
  auto data=core::Document::parse(encode(resume->find("data")),limits);require(data&&data->root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid deletion backup resume data");
  if(const auto* pieces=data->find("resume data")){require(pieces->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid deletion backup piece flags");const std::string flags(pieces->bytes.size(),'\2');require(data->replaceValue("resume data",cdBytes(flags)).has_value(),ErrorCode::InvalidFormat,"Cannot project deletion backup piece flags");}
  require(data->replaceValue("valid","i0e").has_value()&&data->replaceValue("blocks","de").has_value()
      &&resume->replaceValue("data",data->serialize()).has_value()&&envelope.replaceValue("resume",resume->serialize()).has_value(),ErrorCode::InvalidFormat,"Cannot project deletion backup recheck");
  auto attributes=core::Document::parse(encode(envelope.document().find("attributes")),limits);
  require(attributes&&attributes->replaceValue("resumecomplete","i1e").has_value()&&envelope.replaceValue("attributes",attributes->serialize()).has_value(),ErrorCode::InvalidFormat,"Cannot project deletion backup completion");
}
std::map<std::string,std::string> cdMemberAfter(const CdCapture& before,const core::TorrentHash& h,const core::SavedCategoryAssignmentEditResult& edit,const core::SavedCategoryAssignment& original){
  const auto name=h.hex()+".dat";const auto& primary=before.bytes.at(cdKey(1,name));auto updated=EncodedDocument::parse(primary,compatibleConfigLimits());require(updated&&edit.encodedAttributes&&original.source,ErrorCode::InvalidFormat,"Invalid selected deletion state");
  // readTarget captured loadAttributes' full authoritative cache view. Hydrate
  // all three sections in its original order, retaining physical compression.
  for(const auto* key:{"attributes","azureus_properties","azureus_private_properties"}){
    if(const auto* value=original.source->find(key)){const auto& source=original.source->originalBytes();require(updated->replaceValue(key,source.substr(value->begin,value->end-value->begin)).has_value(),ErrorCode::InvalidFormat,"Cannot project authoritative deletion properties");}
  }
  require(updated->replaceValue("attributes",*edit.encodedAttributes).has_value(),ErrorCode::InvalidFormat,"Cannot project deletion state");std::map<std::string,std::string> out{{cdKey(1,name),cdEncoded(updated->serialize())}};
  auto cached=before.bytes.find(cdKey(1,"cache.dat"));bool selected=false;
  if(cached!=before.bytes.end()){
    auto cache=EncodedDocument::parse(cached->second,compatibleConfigLimits());require(cache&&cache->compressed(),ErrorCode::InvalidFormat,"Invalid authoritative deletion cache");const auto* entries=cache->document().find("state");if(entries){require(entries->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Invalid deletion cache entries");std::string retained="l";const auto raw=h.bytes();const std::string_view key(reinterpret_cast<const char*>(raw.data()),raw.size());for(const auto& v:entries->children){const auto* id=v.find("hash");if(id&&id->type==core::Node::Type::Bytes&&id->bytes==key){selected=true;}else{retained+=cache->document().originalBytes().substr(v.begin,v.end-v.begin);}}if(selected){retained+='e';require(cache->replaceValue("state",std::move(retained)).has_value(),ErrorCode::InvalidFormat,"Cannot project deletion cache");out[cdKey(1,"cache.dat")]=cdEncoded(cache->serialize());out[cdKey(1,"cache.dat.bak")]=cached->second;}}
  }
  auto backup=*updated;cdBackupRecheck(backup);out[cdKey(1,name+".bak")]=cdEncoded(backup.serialize());return out;
}
void cdVerifyEffects(const CdCapture& before,const CdCapture& after,const std::map<std::string,std::string>& changed){require(before.rootId==after.rootId&&before.activeId==after.activeId&&before.guards.size()==after.guards.size(),ErrorCode::Conflict,"Deletion source directories changed");for(const auto& [key,old]:before.guards){const auto next=after.guards.find(key);require(next!=after.guards.end(),ErrorCode::Conflict,"Deletion source set changed");if(const auto expected=changed.find(key);expected!=changed.end()){const auto actual=after.bytes.find(key);require(actual!=after.bytes.end()&&actual->second==expected->second,ErrorCode::Conflict,"Category writer postimage differs; preserve review");}else{require(next->second==old,ErrorCode::Conflict,"External edit during category publication; preserve review");}}}
} // namespace
std::expected<CategoryDeletionRecovery,Error> CategoryStore::inspectRemove(
    const CategorySnapshot& expected,std::stop_token stop,Deadline end)const{
  CategoryDeletionRecovery result;
  try{
    cdTake(impl_->current());cdTake(budget(stop,end));
    auto access=cdTake(impl_->lease->sourceAccess());
    const auto defs=cdTake(impl_->fresh(expected));
    if(!categoryJournalPending(impl_->lease->directoryHandle())){return result;}
    result.state=CategoryDeletionRecovery::State::Review;
    for(const auto& name:LegacyCategoryJournalNames){
      require(freshStamp(impl_->lease->directoryHandle(),name,{}),
          ErrorCode::Conflict,"Legacy deletion candidate requires preserved review");
    }
    auto journal=cdReadJournal(*impl_->lease);
    // Display only validated bounded owning fields, even when the physical
    // source below proves this prefix cannot safely continue.
    result.name=journal.plan.name;result.acknowledgedMembers=journal.plan.done;
    result.totalMembers=journal.plan.members.size();result.definitionAcknowledged=journal.plan.published;
    require(defs.writable,ErrorCode::Conflict,"Category companion files require preserved review");
    const auto hashes=cdInventory(journal.plan.inventory);
    cdVerify(journal.plan,cdCapture(*impl_->lease,hashes,journal.plan.version));cdTake(impl_->roots());
    require(equal(journal.inputs,cdJournalInputs(impl_->lease->directoryHandle())),
        ErrorCode::Conflict,"Deletion journal changed during inspection");
    cdTake(budget(stop,end));result.state=CategoryDeletionRecovery::State::Resumable;
    return result;
  }catch(const fileop::Failure& f){
    if(f.error.code==ErrorCode::Cancelled){return std::unexpected(f.error);}
    // An invalid/stale caller is an error, not a fresh source observation.
    if(result.state==CategoryDeletionRecovery::State::None){return std::unexpected(f.error);}
    result.reason=f.error.message.substr(0,1024);return result;
  }catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory inspecting category recovery");}
   catch(...){return fail(ErrorCode::Conflict,"Cannot inspect category recovery; preserve all candidates");}
}
std::expected<CategoryCommit,Error> CategoryStore::remove(const CategorySnapshot& expected,std::string_view name,std::stop_token stop,Deadline end,CategoryDeletionObserver observer){return removeInternal(expected,name,false,stop,end,observer);}
std::expected<CategoryCommit,Error> CategoryStore::resumeRemove(const CategorySnapshot& expected,std::string_view name,std::stop_token stop,Deadline end,CategoryDeletionObserver observer){return removeInternal(expected,name,true,stop,end,observer);}
std::expected<CategoryCommit,Error> CategoryStore::removeInternal(const CategorySnapshot& expected,std::string_view name,bool resume,std::stop_token stop,Deadline end,CategoryDeletionObserver observer){
  bool attempted=false;struct Reset {Impl* p;bool armed=false;~Reset(){if(armed){p->deleting=false;p->observing=false;}}} reset{impl_.get()};
  try{
    cdTake(impl_->current());cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));require(!impl_->deleting,ErrorCode::Conflict,"Category deletion is already active");cdTake(budget(stop,end));auto access=cdTake(impl_->lease->sourceAccess());auto defs=cdTake(impl_->fresh(expected));require(defs.writable,ErrorCode::Conflict,"Resolve category companion files before deletion");require(impl_->generation<std::numeric_limits<std::uint64_t>::max(),ErrorCode::Conflict,"Category generations exhausted");impl_->deleting=true;reset.armed=true;
    std::optional<CdJournalFile> journal;std::vector<core::TorrentHash> hashes;
    if(resume){
      for(const auto& candidate:LegacyCategoryJournalNames){
        require(freshStamp(impl_->lease->directoryHandle(),candidate,{}),
            ErrorCode::Conflict,"Legacy deletion candidate requires preserved review");
      }
      journal.emplace(cdReadJournal(*impl_->lease));require(journal->plan.name==name,ErrorCode::Conflict,"Explicit deletion selection differs from pending journal");hashes=cdInventory(journal->plan.inventory);}
    else{
      require(!categoryJournalPending(impl_->lease->directoryHandle()),ErrorCode::Conflict,"Existing deletion requires explicit recovery/review");auto edit=core::editSavedCategories(defs.value.source->originalBytes(),core::SavedCategoryDelete{std::string(name)});require(edit&&edit->changed&&edit->clearResolvedAssignments,ErrorCode::InvalidFormat,"A current user category is required for deletion");
      auto inventory=cdTake(capture(impl_->lease->directoryHandle(),InventoryNames[0],true,CdDocument));require(inventory.bytes.has_value(),ErrorCode::Conflict,"Complete primary downloads inventory required before deletion");hashes=cdInventory(*inventory.bytes);
      constexpr int version=2;auto before=cdCapture(*impl_->lease,hashes,version);require(!before.guards.at(cdKey(0,"downloads.config.saving")).present&&!before.guards.at(cdKey(1,"cache.dat.saving")).present&&!before.guards.at(cdKey(1,"cache.dat.bak.saving")).present,ErrorCode::Conflict,"Resolve pending inventory/cache writes before deletion");
      require(before.guards.at(cdKey(1,"cache.dat")).present||!before.guards.at(cdKey(1,"cache.dat.bak")).present,ErrorCode::Conflict,"Missing primary cache with backup requires review");
      std::size_t capturedBytes=0;for(const auto& [key,value]:before.bytes){static_cast<void>(key);capturedBytes+=value.size();}require(capturedBytes<=CdTotal/4,ErrorCode::Unsupported,"Deletion input set needs bounded backup/postimage headroom");
      CdPlan plan;plan.name=std::string(name);plan.definitionsBefore=defs.value.source->originalBytes();plan.definitionsAfter=edit->value.source->originalBytes();plan.inventory=*inventory.bytes;plan.rootId=before.rootId;plan.activeId=before.activeId;plan.guards=before.guards;
      if (impl_->tagIds) {
        const auto id=impl_->tagIds->find(name);
        require(id!=impl_->tagIds->end(),ErrorCode::Conflict,"Selected category has no live tag identity");
        plan.categoryTagId=id->second;
      }
      // Validate the original type-1 edit while all untouched preimages are
      // still available. The full postimage is rebuilt from the guarded bytes.
      static_cast<void>(cdTagRetirement(plan,before));
      for(const auto& hash:hashes){auto target=cdTake(readTarget(expected,hash,stop,end));require(target.writable(),ErrorCode::Conflict,"Every inventory state must be provided, primary and writable");if(target.assignment()->resolved&&target.assignment()->declaredName==name){plan.members.push_back(hash.hex());}}
      cdTake(impl_->roots());cdVerify(plan,cdCapture(*impl_->lease,hashes,plan.version));cdTake(budget(stop,end));const auto encoded=cdDict({{"state",cdState(plan)}});require(encoded.size()<=CdJournal,ErrorCode::Unsupported,"Deletion journal exceeds bound");access=cdTake(impl_->lease->sourceAccess(true));cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));cdVerify(plan,cdCapture(*impl_->lease,hashes,plan.version));attempted=true;cdTake(ConfigFile::createMissing(*impl_->lease,CategoryJournalNames[0],encoded,cdLimits()));journal.emplace(cdReadJournal(*impl_->lease));
    }
    attempted=true;
    const auto captureCurrent=[&]{return cdCapture(*impl_->lease,hashes,journal->plan.version);};
    const auto verify=[&]{cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));cdTake(impl_->roots());cdVerify(journal->plan,captureCurrent());auto input=cdJournalInputs(impl_->lease->directoryHandle());require(equal(journal->inputs,input),ErrorCode::Conflict,"Deletion journal source changed");};
    const auto observe=[&](CategoryDeletionStage stage){if(observer){impl_->observing=true;try{access.outside([&]{observer(stage,journal->plan.done);});}catch(...){impl_->observing=false;throw;}impl_->observing=false;}cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));};
    verify();if(resume){access=cdTake(impl_->lease->sourceAccess(true));}if(!resume){observe(CategoryDeletionStage::JournalSynced);verify();}
    if(!journal->plan.published){
      while(journal->plan.done<journal->plan.members.size()){
        cdTake(budget(stop,end));verify();const auto h=cdMemberHash(journal->plan.members[journal->plan.done]);auto target=cdTake(readTarget(expected,h,stop,end));require(target.writable()&&target.assignment()->declaredName==journal->plan.name&&target.assignment()->resolved,ErrorCode::Conflict,"Pending member no longer has the selected category");
        auto oldDefinitions=core::readSavedCategories(journal->plan.definitionsBefore);require(bool(oldDefinitions),ErrorCode::InvalidFormat,"Cannot revalidate deletion definition source");core::SavedCategoryRemovalIntent intent{journal->plan.name,oldDefinitions->source};auto edit=core::clearDeletedCategoryAssignment(intent,target.assignment()->source->originalBytes());require(edit&&edit->changed,ErrorCode::Conflict,"Pending category clear is no longer valid");const auto before=captureCurrent();cdVerify(journal->plan,before);const auto effects=cdMemberAfter(before,h,*edit,*target.assignment());
        cdTake(assign(expected,target,std::nullopt,stop,end));observe(CategoryDeletionStage::MemberWritten);cdTake(impl_->roots());auto after=captureCurrent();cdVerifyEffects(before,after,effects);auto progress=journal->plan;progress.guards=after.guards;++progress.done;cdCheckpoint(*impl_->lease,*journal,std::move(progress));observe(CategoryDeletionStage::MemberAcknowledged);verify();
      }
      // Recheck every known current state, not only originally matching members.
      for(const auto& hash:hashes){auto target=cdTake(readTarget(expected,hash,stop,end));require(target.assignment()->declaredName!=journal->plan.name,ErrorCode::Conflict,"An uncleared provided category member remains");}
      cdTake(budget(stop,end));verify();auto before=captureCurrent();const auto& bytes=journal->plan.definitionsAfter;cdTake(budget(stop,end));
      const auto directory=impl_->lease->directoryHandle();
      const auto original=cdTake(stateStampAt(directory,CategoryNames[0]));
      require(original&&cdStamp(*original)==before.guards.at(cdKey(0,CategoryNames[0])).stamp,
          ErrorCode::Conflict,"Deletion definition changed before staging");
      const auto staged=cdTake(stageCategoryDefinition(directory,bytes,detail::stateWriteMode(*original)));
      // Our .new now exists, so compare every other source guard before rename.
      auto withNew=captureCurrent();
      cdVerifyEffects(before,withNew,{{cdKey(0,CategoryNames[1]),bytes}});
      cdTake(impl_->roots());
      cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));
      cdTake(publishCategoryDefinition(directory,staged,true));
      observe(CategoryDeletionStage::DefinitionWritten);
      auto after=captureCurrent();
      cdVerifyEffects(before,after,{{cdKey(0,CategoryNames[0]),bytes}});
      const auto published=cdTake(stateStampAt(directory,CategoryNames[0]));
      require(published&&published->links==1&&detail::sameStateIdentity(staged,*published),
          ErrorCode::Conflict,"Deleted definition primary is not the staged file");
      auto progress=journal->plan;progress.published=true;progress.guards=after.guards;
      cdCheckpoint(*impl_->lease,*journal,std::move(progress));
      observe(CategoryDeletionStage::DefinitionAcknowledged);verify();
    }
    verify();
    const auto beforeTags=captureCurrent();
    cdVerify(journal->plan,beforeTags);
    if (auto retired=cdTagRetirement(journal->plan,beforeTags)) {
      cdTake(budget(stop,end));
      const auto primary=cdKey(0,CategoryTagNames[0]);
      const auto backup=cdKey(0,CategoryTagNames[1]);
      auto file=cdTake(ConfigFile::load(*impl_->lease,CategoryTagNames[0],core::Limits{}));
      require(file.recoverySource()==RecoverySource::Primary
          &&file.document().originalBytes()==beforeTags.bytes.at(primary),
          ErrorCode::Conflict,"Category tag configuration source changed");
      const auto edited=core::Document::parse(*retired);
      const auto* type=edited?edited->find("1"):nullptr;
      require(type&&file.document().replaceValue("1",retired->substr(type->begin,type->end-type->begin))
          &&file.document().serialize()==*retired,ErrorCode::InvalidFormat,"Cannot encode exact category tag retirement");
      verify();
      cdTake(file.save(*impl_->lease));
      observe(CategoryDeletionStage::TagMetadataWritten);
      cdTake(impl_->roots());
      auto after=captureCurrent();
      cdVerifyEffects(beforeTags,after,{{primary,*retired},{backup,beforeTags.bytes.at(primary)}});
      const auto& oldPrimary=beforeTags.guards.at(primary).stamp;
      const auto& actualBackup=after.guards.at(backup).stamp;
      require(actualBackup[0]==oldPrimary[0]&&actualBackup[1]==oldPrimary[1],
          ErrorCode::Conflict,"Category tag backup does not retain the guarded primary");
      auto progress=journal->plan;
      progress.guards=std::move(after.guards);
      cdCheckpoint(*impl_->lease,*journal,std::move(progress));
      observe(CategoryDeletionStage::TagMetadataAcknowledged);
      verify();
    }
    auto actual=cdTake(definitions(*impl_->lease));require(actual.value.source->originalBytes()==journal->plan.definitionsAfter,ErrorCode::Conflict,"Deleted definition not verified");
    // Cleanup only exact owned journal inputs. A surviving changed candidate is
    // review, never silently unlinked. Primary is removed last.
    for(const auto i:{1U,0U}){
      const auto& input=journal->inputs[i];if(!input.stamp){continue;}
      const auto directory=impl_->lease->directoryHandle();
      const auto now=cdTake(capture(directory,CategoryJournalNames[i],true,CdJournal));
      require(input.matches(now),ErrorCode::Conflict,"Deletion journal cleanup source changed");
      cdTake(requireNoPendingSubscriptionAcquisition(*impl_->lease));
      cdTake(detail::removePayloadName(directory,CategoryJournalNames[i],*input.stamp));
      cdTake(detail::syncStateDirectory(directory));
    }
    impl_->publish(std::move(actual),true);observe(CategoryDeletionStage::Completed);return CategoryCommit{CategorySnapshot(impl_->latest),true,{}};
  }catch(const fileop::Failure& f){if(attempted){impl_->poison();}return std::unexpected(f.error);}catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"Insufficient memory; category deletion may require preserved review");}catch(...){if(attempted){impl_->poison();}return fail(ErrorCode::Conflict,"Category deletion interrupted; preserve journal for explicit review");}
}
