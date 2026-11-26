// Fixed owned paths only, under the actual profile/source gate.
namespace state = storage::detail;
using state::StateHandle;
using FileValue = search::profile_files::File;

FileValue readFile(StateHandle dir, std::string name, std::size_t cap = AcquisitionBytes) {
  auto captured = search::profile_files::named(dir, std::move(name), cap);
  require(captured.has_value(), storage::ErrorCode::Conflict, "Acquisition file ownership, identity or bounds changed");
  return std::move(*captured);
}

bool named(StateHandle dir, const FileValue& value) {
  const auto label = state::stateStampAt(dir, value.name);
  return label && label->has_value() == value.present && (!value.present
      || (value.witness.stillHeld() && state::sameStateSnapshot(*value.witness.stamp, **label)));
}

std::optional<std::string> contents(const FileValue& value) {
  return value.present ? std::optional{value.bytes} : std::nullopt;
}

void removeOwned(StateHandle dir, const FileValue& value) {
  if (!value.present) { return; }
  require(named(dir, value), storage::ErrorCode::Conflict, "Owned acquisition pathname changed before removal");
  take(state::retireStateFile(dir, value.name, *value.witness.stamp));
}

void synchronize(StateHandle dir) { take(state::syncStateDirectory(dir)); }

void writeNew(StateHandle dir, const std::string& name, std::string_view bytes) {
  require(bytes.size() <= AcquisitionBytes, storage::ErrorCode::InvalidFormat, "Acquisition intent exceeds bound");
  auto file = take(state::stageStateFile(dir, name, bytes, 0600));
  take(state::protectPrivateStateFile(file.get()));
  take(state::syncStateFile(file.get()));
}

void publishOwned(StateHandle dir, const FileValue& file, const std::string& name, bool replace) {
  require(file.present && named(dir, file), storage::ErrorCode::Conflict, "Acquisition publication source changed");
  take(state::publishStateFile(dir, file.name, name, state::InvalidStateHandle, replace, &*file.witness.stamp));
}

void writeValue(StateHandle dir, const std::string& name, const std::string& desired) {
  const auto backupName = name + ".bak";
  const auto savingName = name + ".saving";
  auto before = readFile(dir, name);
  auto backup = readFile(dir, backupName);
  auto saving = readFile(dir, savingName);
  if (before.present && before.bytes == desired) {
    removeOwned(dir, saving);
    synchronize(dir);
    return;
  }
  if (saving.present && saving.bytes != desired) {
    removeOwned(dir, saving);
    saving = readFile(dir, savingName);
  }
  if (!saving.present) {
    writeNew(dir, savingName, desired);
    saving = readFile(dir, savingName);
  }
  require(named(dir, before) && named(dir, backup) && saving.bytes == desired,
      storage::ErrorCode::Conflict, "Acquisition publication names changed");
  if (before.present) {
    removeOwned(dir, backup);
    publishOwned(dir, before, backupName, false);
  }
  publishOwned(dir, saving, name, false);
  synchronize(dir);
}
enum class IntentMode {LegacyFresh,Fresh,Existing,AutomaticFresh,AutomaticExisting};
bool automaticMode(IntentMode mode){return mode==IntentMode::AutomaticFresh||mode==IntentMode::AutomaticExisting;}
bool existingMode(IntentMode mode){return mode==IntentMode::Existing||mode==IntentMode::AutomaticExisting;}
struct Intent {
  std::string source,selection,plan,nonce,tempDirectory,tempScope,tempHash;
  bool stopped=false,done=false;std::optional<std::int64_t> when;std::optional<AssociationReadValues> values;
  IntentMode mode=IntentMode::LegacyFresh;
  std::string destination,ownerSession,targetHash,filenameConversions,stateBaseSha256;
  bool decoderShowAll=false,tempRetired=false,newAssociation=false;
};
std::string encodeIntent(const Intent& in){
  const bool legacy=in.mode==IntentMode::LegacyFresh;
  std::map<std::string,std::string> fields{{"schema",stringValue(legacy?"BiglyBTC.Subscription.Acquisition.V1":automaticMode(in.mode)?"BiglyBTC.Subscription.Acquisition.V3":"BiglyBTC.Subscription.Acquisition.V2")},{"source",stringValue(in.source)},{"selection",stringValue(in.selection)},{"plan",stringValue(in.plan)},{"nonce",stringValue(in.nonce)},{"temp_directory",stringValue(in.tempDirectory)},{"temp_scope",stringValue(in.tempScope)},{"temp_hash",stringValue(in.tempHash)},{"stop_subscription",integerValue(in.stopped?1:0)},{"done",integerValue(in.done?1:0)}};
  if(!legacy){fields["mode"]=stringValue(existingMode(in.mode)?"existing":"fresh");fields["destination"]=stringValue(in.destination);fields["owner_session"]=stringValue(in.ownerSession);fields["target_hash"]=stringValue(in.targetHash);fields["filename_conversions"]=stringValue(in.filenameConversions);fields["decoder_show_all"]=integerValue(in.decoderShowAll?1:0);fields["temp_retired"]=integerValue(in.tempRetired?1:0);fields["state_base_sha256"]=stringValue(in.stateBaseSha256);fields["new_association"]=integerValue(in.newAssociation?1:0);}
  if(in.values){require(in.when.has_value(),storage::ErrorCode::InvalidFormat,"Acquisition commit time missing");fields["when"]=integerValue(*in.when);fields["A"]=stringValue(in.values->association);fields["R"]=stringValue(in.values->results);fields["F"]=stringValue(in.values->finalConfiguration);}return dictionaryValue(fields);
}
std::uint64_t attemptSession(const Intent& in){std::uint64_t result=0;const auto parsed=std::from_chars(in.ownerSession.data(),in.ownerSession.data()+in.ownerSession.size(),result);require(parsed.ec==std::errc{}&&parsed.ptr==in.ownerSession.data()+in.ownerSession.size()&&result&&std::to_string(result)==in.ownerSession,storage::ErrorCode::InvalidFormat,"Acquisition attempt session invalid");return result;}
core::TorrentHash intentTarget(const Intent& in){auto hash=core::TorrentHash::parse(in.targetHash);require(hash&&hash->bytes().size()==20,storage::ErrorCode::InvalidFormat,"Acquisition target identity invalid");return *hash;}
Intent parseIntent(std::string raw){
  auto root=document(std::move(raw));const auto& node=root.root();const auto schema=field(node,"schema");require(schema=="BiglyBTC.Subscription.Acquisition.V1"||schema=="BiglyBTC.Subscription.Acquisition.V2"||schema=="BiglyBTC.Subscription.Acquisition.V3",storage::ErrorCode::InvalidFormat,"Acquisition schema unsupported");Intent out;
  out.source=field(node,"source");out.selection=field(node,"selection");out.plan=field(node,"plan");out.nonce=field(node,"nonce");out.tempDirectory=field(node,"temp_directory");out.tempScope=field(node,"temp_scope");out.tempHash=field(node,"temp_hash");const auto stopped=integer(node,"stop_subscription"),done=integer(node,"done");require((stopped==0||stopped==1)&&(done==0||done==1)&&!out.nonce.empty()&&out.nonce.size()<=32&&out.tempHash.size()==64,storage::ErrorCode::InvalidFormat,"Acquisition state invalid");out.stopped=stopped==1;out.done=done==1;
  if(node.find("A")||node.find("R")||node.find("F")||node.find("when")){out.when=integer(node,"when");out.values=AssociationReadValues{field(node,"A"),field(node,"R"),field(node,"F")};}
  if(schema!="BiglyBTC.Subscription.Acquisition.V1"){
    const auto mode=field(node,"mode");require(mode=="fresh"||mode=="existing",storage::ErrorCode::InvalidFormat,"Acquisition mode unsupported");out.mode=schema=="BiglyBTC.Subscription.Acquisition.V3"?(mode=="fresh"?IntentMode::AutomaticFresh:IntentMode::AutomaticExisting):(mode=="fresh"?IntentMode::Fresh:IntentMode::Existing);
    out.destination=field(node,"destination");out.ownerSession=field(node,"owner_session");out.targetHash=field(node,"target_hash");out.filenameConversions=field(node,"filename_conversions");out.stateBaseSha256=field(node,"state_base_sha256");const auto decoder=integer(node,"decoder_show_all"),retired=integer(node,"temp_retired"),added=integer(node,"new_association");require((decoder==0||decoder==1)&&(retired==0||retired==1)&&(added==0||added==1)&&out.destination.size()<=4096&&out.filenameConversions.size()<=65536&&out.stateBaseSha256.size()==64&&std::ranges::all_of(out.stateBaseSha256,[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),storage::ErrorCode::InvalidFormat,"Acquisition V2 field invalid");out.decoderShowAll=decoder==1;out.tempRetired=retired==1;out.newAssociation=added==1;if(out.values){out.values->newAssociation=out.newAssociation;}(void)attemptSession(out);(void)intentTarget(out);require(!out.tempRetired||out.done||out.stopped,storage::ErrorCode::InvalidFormat,"Retired acquisition temp lacks a terminal disposition");
  }
  require(!(out.done&&out.stopped)&&(!out.done||out.values.has_value()),storage::ErrorCode::InvalidFormat,"Acquisition terminal state invalid");require(encodeIntent(out)==root.originalBytes(),storage::ErrorCode::InvalidFormat,"Acquisition encoding is not closed");return out;
}
// V3 permits only one monotone refinement: storage captures its created stage
// inode and actual backup/scope before publication. Every other recipe byte
// remains fixed; restore independently authenticates the selected refined plan.
bool automaticRecipeExtends(const std::string& before,const std::string& after){
  if(before==after){return true;}auto a=document(before),z=document(after);
  const auto schema=field(a.root(),"schema");if(schema!=field(z.root(),"schema")||(schema!="BiglyBTC.StoppedPublicSubscriptionIntake.V3"&&schema!="BiglyBTC.ExistingStoppedSubscriptionTarget.V2")){return false;}
  if(a.root().children.size()!=z.root().children.size()){return false;}
  for(std::size_t n=0;n<a.root().children.size();n+=2){const auto key=a.root().children[n].bytes;if(key=="automatic"){continue;}const auto* other=z.root().find(key);if(!other||a.originalBytes().substr(a.root().children[n+1].begin,a.root().children[n+1].end-a.root().children[n+1].begin)!=z.originalBytes().substr(other->begin,other->end-other->begin)){return false;}}
  auto x=document(field(a.root(),"automatic")),y=document(field(z.root(),"automatic"));if(x.root().children.size()!=y.root().children.size()){return false;}
  for(std::size_t n=0;n<x.root().children.size();n+=2){const auto key=x.root().children[n].bytes;const auto& first=x.root().children[n+1];const auto* second=y.root().find(key);if(!second){return false;}
    const bool mutableField=key=="created"||key=="copyParentScope"||key=="queueBound"||key=="inventoryBackup";
    if(mutableField&&first.type==core::Node::Type::Integer&&first.integer==0){continue;}
    if(x.originalBytes().substr(first.begin,first.end-first.begin)!=y.originalBytes().substr(second->begin,second->end-second->begin)){return false;}
  }return true;
}
bool extends(const Intent& before,const Intent& after){return before.source==after.source&&before.selection==after.selection&&(before.plan==after.plan||(automaticMode(before.mode)&&automaticRecipeExtends(before.plan,after.plan)))&&before.nonce==after.nonce&&before.tempDirectory==after.tempDirectory&&before.tempScope==after.tempScope&&before.tempHash==after.tempHash&&before.mode==after.mode&&before.destination==after.destination&&before.ownerSession==after.ownerSession&&before.targetHash==after.targetHash&&before.filenameConversions==after.filenameConversions&&before.decoderShowAll==after.decoderShowAll&&before.stateBaseSha256==after.stateBaseSha256&&before.newAssociation==after.newAssociation&&(!before.tempRetired||after.tempRetired)&&(!before.stopped||after.stopped)&&(!before.done||after.done)&&(!before.values||(after.values&&before.when==after.when&&before.values->association==after.values->association&&before.values->results==after.values->results&&before.values->finalConfiguration==after.values->finalConfiguration));}
void publishIntent(StateHandle root, std::string_view expected, const Intent& in) {
  const std::string owner(OwnerName);
  const auto primary = readFile(root, owner);
  const auto backup = readFile(root, owner + ".bak");
  const auto saving = readFile(root, owner + ".saving");
  require(!backup.present && !saving.present && primary.present == !expected.empty()
      && (!primary.present || primary.bytes == expected), storage::ErrorCode::Conflict, "Acquisition owner intent changed");
  const auto bytes = encodeIntent(in);
  writeNew(root, saving.name, bytes);
  const auto staged = readFile(root, saving.name);
  require(named(root, primary) && staged.bytes == bytes, storage::ErrorCode::Conflict, "Acquisition owner publication changed");
  publishOwned(root, staged, owner, primary.present);
  synchronize(root);
}

void removeIntent(StateHandle root, std::string_view expected) {
  const auto value = readFile(root, std::string(OwnerName));
  require(value.present && value.bytes == expected, storage::ErrorCode::Conflict, "Acquisition owner changed before acknowledgement");
  removeOwned(root, value);
  synchronize(root);
}

state::StateDescriptor checkedDirectory(const std::filesystem::path& path) {
  auto opened = take(state::openStatePathDirectory(path));
  const auto held = take(state::stateStamp(opened.get()));
  require(held.directory && held.owned, storage::ErrorCode::UnsafePath, "Acquisition directory is not owned");
  return opened;
}

void profileCurrent(const storage::ProfileLease& lease, const std::filesystem::path& path) {
  require(search::profile_files::directory(lease, path).has_value(), storage::ErrorCode::UnsafePath,
      "Acquisition profile directory changed");
}

state::StateDescriptor sourceDirectory(const storage::ProfileLease& lease, const core::Node& source) {
  const auto root = take(state::stateStamp(lease.directoryHandle()));
  require(root.directory && root.owned && scope(root) == field(source, "root_scope"),
      storage::ErrorCode::Conflict, "Acquisition profile identity changed");
  auto opened = take(state::openStateDirectory(lease.directoryHandle(), "subs"));
  const auto child = take(state::stateStamp(opened.get()));
  const auto label = take(state::stateStampAt(lease.directoryHandle(), "subs"));
  require(label && child.owned && state::sameStateDirectory(child, *label) && scope(child) == field(source, "subs_scope"),
      storage::ErrorCode::Conflict, "Acquisition subscription directory changed");
  return opened;
}

bool staticBindings(StateHandle root, StateHandle child, const core::Node& source) {
  const auto labels = [&] {
    const auto path = platform::pathFromUtf8(field(source, "profile"));
    const auto directory = checkedDirectory(path);
    const auto heldRoot = take(state::stateStamp(root));
    const auto namedRoot = take(state::stateStamp(directory.get()));
    const auto heldChild = take(state::stateStamp(child));
    const auto namedChild = take(state::stateStampAt(root, "subs"));
    return heldRoot.owned && scope(heldRoot) == field(source, "root_scope")
        && state::sameStateDirectory(heldRoot, namedRoot)
        && namedChild && heldChild.owned && scope(heldChild) == field(source, "subs_scope")
        && state::sameStateDirectory(heldChild, *namedChild);
  };
  const auto check = [](StateHandle dir, const core::Node* entries) {
    require(entries && entries->type == core::Node::Type::Dictionary, storage::ErrorCode::InvalidFormat, "Acquisition bindings missing");
    for (std::size_t index = 0; index < entries->children.size(); index += 2) {
      const std::string name(entries->children[index].bytes);
      require(!name.empty() && name.find('/') == name.npos && name != "." && name != "..",
          storage::ErrorCode::InvalidFormat, "Acquisition binding name invalid");
      const auto actual = readFile(dir, name, 4 * 1024 * 1024);
      const auto& expected = entries->children[index + 1];
      if (expected.type == core::Node::Type::Integer && expected.integer == 0) {
        if (actual.present) { return false; }
      } else {
        require(expected.type == core::Node::Type::Dictionary, storage::ErrorCode::InvalidFormat, "Acquisition binding type invalid");
        if (!actual.present || actual.bytes != field(expected, "bytes")
            || stamp(*actual.witness.stamp) != field(expected, "stamp")) { return false; }
      }
    }
    return true;
  };
  require(labels(), storage::ErrorCode::Conflict, "Acquisition directory labels changed");
  const bool unchanged = check(root, source.find("roots")) && check(child, source.find("children"));
  require(labels(), storage::ErrorCode::Conflict, "Acquisition directory labels changed");
  return unchanged;
}
void knownFamily(StateHandle dir,const std::string& name,const std::vector<std::string>& primary,const std::optional<std::string>& oldBackup){auto current=readFile(dir,name,4*1024*1024),backup=readFile(dir,name+".bak",4*1024*1024),saving=readFile(dir,name+".saving",4*1024*1024);const auto known=[&](const FileValue& f){return f.present&&std::ranges::find(primary,f.bytes)!=primary.end();};require(known(current)||(!current.present&&known(backup)&&known(saving)),storage::ErrorCode::Conflict,"Unknown acquisition subscription primary");require(!backup.present||known(backup)||(oldBackup&&backup.bytes==*oldBackup),storage::ErrorCode::Conflict,"Unknown acquisition subscription backup");require(!saving.present||known(saving),storage::ErrorCode::Conflict,"Unknown acquisition subscription saving file");}
std::filesystem::path tempBase() {
#ifdef _WIN32
  std::error_code error;
  auto path = std::filesystem::temp_directory_path(error);
  require(!error, storage::ErrorCode::Io, "Acquisition temporary directory is unavailable");
  path = std::filesystem::canonical(path, error);
  require(!error, storage::ErrorCode::UnsafePath, "Acquisition temporary directory cannot be resolved");
  return path;
#else
  return "/tmp";
#endif
}

void cleanupTemporary(const Intent& in) {
  const auto path = platform::pathFromUtf8(in.tempDirectory);
  const auto base = tempBase();
  std::error_code error;
  const auto actualBase = std::filesystem::canonical(base, error);
  require(!error, storage::ErrorCode::UnsafePath, "Acquisition temporary directory cannot be resolved");
  require(path.is_absolute() && (path.parent_path() == base || path.parent_path() == actualBase)
      && platform::pathToUtf8(path.filename()).starts_with("biglybtc-sub-acq-"),
      storage::ErrorCode::UnsafePath, "Acquisition temporary path invalid");
  const auto parent = take(state::openStatePathDirectory(actualBase));
  const auto leaf = path.filename();
  const auto label = take(state::stateStampAt(parent.get(), leaf));
  if (!label) { return; }
  auto dir = take(state::openDirectoryAt(parent.get(), leaf));
  const auto held = take(state::stateStamp(dir.get()));
  require(held.owned && state::sameStateDirectory(held, *label) && scope(held) == in.tempScope,
      storage::ErrorCode::Conflict, "Acquisition temporary directory changed");
  {
    const auto file = readFile(dir.get(), "metadata.torrent", 4 * 1024 * 1024);
    if (file.present) {
      require(digest(file.bytes) == in.tempHash, storage::ErrorCode::Conflict, "Acquisition temporary metadata changed");
      removeOwned(dir.get(), file);
    }
  }
  dir = state::StateDescriptor{};
  take(state::retireEmptyDirectoryAt(parent.get(), leaf, held));
}
// Exact protocol states, including our writer's own crash cuts. Independent
// per-file membership cannot prove an ordered two-file A/R/F transaction.
struct FamilyView {std::optional<std::string_view> primary,backup,saving;bool operator==(const FamilyView&)const=default;};
struct PairView {FamilyView config,results;bool operator==(const PairView&)const=default;};
struct PairStep {PairView files;unsigned next=0;};
std::optional<std::string_view> optionalView(const std::optional<std::string>& bytes){return bytes?std::optional<std::string_view>{*bytes}:std::nullopt;}
void writerStates(PairView& pair,bool configuration,std::string_view desired,unsigned next,std::vector<PairStep>& states){
  auto& file=configuration?pair.config:pair.results;
  const auto append=[&]{states.push_back({pair,next});};
  if(file.primary==desired){file.saving.reset();append();return;}
  if(file.saving&&file.saving!=desired){file.saving.reset();append();}
  if(!file.saving){file.saving=desired;append();}
  if(file.primary){file.backup.reset();append();file.backup=file.primary;file.primary.reset();append();}
  file.primary=file.saving;file.saving.reset();append();
}
unsigned logicalProgress(StateHandle root,StateHandle child,const std::string& name,const std::string& beforeConfig,const std::optional<std::string>& configBackup,const std::string& beforeResults,const std::optional<std::string>& resultsBackup,const std::optional<AssociationReadValues>& values,bool stopped,bool done){
  std::array<FileValue,3> config{readFile(root,"subscriptions.config",4*1024*1024),readFile(root,"subscriptions.config.bak",4*1024*1024),readFile(root,"subscriptions.config.saving",4*1024*1024)};
  std::array<FileValue,3> results{readFile(child,name,4*1024*1024),readFile(child,name+".bak",4*1024*1024),readFile(child,name+".saving",4*1024*1024)};
  const auto family=[](const auto& files){const auto view=[](const auto& file)->std::optional<std::string_view>{return file.present?std::optional<std::string_view>{file.bytes}:std::nullopt;};return FamilyView{view(files[0]),view(files[1]),view(files[2])};};
  const PairView actual{family(config),family(results)};
  PairView cursor{{beforeConfig,optionalView(configBackup),{}},{beforeResults,optionalView(resultsBackup),{}}};
  std::vector<PairStep> forward{{cursor,0}};
  if(values){writerStates(cursor,true,values->association,0,forward);forward.push_back({cursor,1});writerStates(cursor,false,values->results,1,forward);forward.push_back({cursor,2});writerStates(cursor,true,values->finalConfiguration,2,forward);forward.push_back({cursor,3});}
  std::optional<unsigned> matched;
  for(const auto& candidate:forward){if(candidate.files==actual&&(!done||candidate.next==3)){matched=std::max(matched.value_or(0),candidate.next);}}
  if(stopped&&!done){
    for(const auto& candidate:forward){auto rollback=candidate.files;std::vector<PairStep> cuts{{rollback,0}};writerStates(rollback,true,beforeConfig,0,cuts);writerStates(rollback,false,beforeResults,0,cuts);if(std::ranges::any_of(cuts,[&](const auto& cut){return cut.files==actual;})){matched=0;break;}}
  }
  require(matched.has_value(),storage::ErrorCode::Conflict,"Subscription files are not a reachable owned A/R/F pair");return *matched;
}
// V2 has one fixed extra state channel. The storage publisher atomically replaces
// primary through known .saving bytes while preserving the captured backup.
struct CallbackFiles {PairView sub;FamilyView state,inventory;bool operator==(const CallbackFiles&)const=default;};
struct CallbackCut {CallbackFiles files;unsigned next=0;};
enum class CallbackAction {PrepareCache,Association,DownloadState,Read,Final,Queue};
std::vector<CallbackAction> callbackActions(bool existing,bool automaticQueue=false){std::vector<CallbackAction> result;if(existing){result.push_back(CallbackAction::PrepareCache);}result.insert(result.end(),{CallbackAction::Association,CallbackAction::DownloadState,CallbackAction::Read,CallbackAction::Final});if(automaticQueue){result.push_back(CallbackAction::Queue);}return result;}
void callbackWriterStates(CallbackFiles& cursor,CallbackAction action,std::string_view desired,unsigned next,std::vector<CallbackCut>& cuts){
  if(action==CallbackAction::Association||action==CallbackAction::Read||action==CallbackAction::Final){std::vector<PairStep> states;writerStates(cursor.sub,action!=CallbackAction::Read,desired,next,states);for(const auto& state:states){auto value=cursor;value.sub=state.files;cuts.push_back({value,next});}return;}
  const auto append=[&]{cuts.push_back({cursor,next});};auto& file=action==CallbackAction::Queue?cursor.inventory:cursor.state;
  if(file.primary==desired){file.saving.reset();append();return;}
  if(file.saving&&file.saving!=desired){file.saving.reset();append();}
  if(!file.saving){file.saving=desired;append();}
  file.primary=file.saving;file.saving.reset();append();
}
struct AutomaticQueueImages {std::string_view before,after;std::optional<std::string_view> backup;};
unsigned logicalCallbackProgress(StateHandle root,StateHandle child,StateHandle active,const std::string& resultName,const std::string& stateName,const std::string& beforeConfig,const std::optional<std::string>& configBackup,const std::string& beforeResults,const std::optional<std::string>& resultsBackup,std::string_view stateBase,std::string_view statePrepared,std::string_view stateAssociated,std::optional<std::string_view> stateBackup,const AssociationReadValues& values,bool existing,bool stopped,bool done,std::optional<AutomaticQueueImages> queue={}){
  std::array<FileValue,3> configs{readFile(root,"subscriptions.config",4*1024*1024),readFile(root,"subscriptions.config.bak",4*1024*1024),readFile(root,"subscriptions.config.saving",4*1024*1024)};
  std::array<FileValue,3> results{readFile(child,resultName,4*1024*1024),readFile(child,resultName+".bak",4*1024*1024),readFile(child,resultName+".saving",4*1024*1024)};
  std::array<FileValue,3> state{readFile(active,stateName,16*1024*1024),readFile(active,stateName+".bak",16*1024*1024),readFile(active,stateName+".saving",16*1024*1024)};
  const auto family=[](const auto& files){const auto view=[](const auto& file)->std::optional<std::string_view>{return file.present?std::optional<std::string_view>{file.bytes}:std::nullopt;};return FamilyView{view(files[0]),view(files[1]),view(files[2])};};std::array<FileValue,3> inventories;if(queue){inventories={readFile(root,"downloads.config",64*1024*1024),readFile(root,"downloads.config.bak",64*1024*1024),readFile(root,"downloads.config.saving",64*1024*1024)};}
  const CallbackFiles actual{{family(configs),family(results)},family(state),queue?family(inventories):FamilyView{}};
  CallbackFiles cursor{{{beforeConfig,optionalView(configBackup),{}},{beforeResults,optionalView(resultsBackup),{}}},{stateBase,stateBackup,{}},queue?FamilyView{queue->before,queue->backup,{}}:FamilyView{}};std::vector<CallbackCut> forward{{cursor,0}};const auto actions=callbackActions(existing,queue.has_value());
  for(unsigned n=0;n<actions.size();++n){const auto action=actions[n];const std::string_view desired=action==CallbackAction::PrepareCache?statePrepared:action==CallbackAction::DownloadState?stateAssociated:action==CallbackAction::Association?values.association:action==CallbackAction::Read?values.results:action==CallbackAction::Queue?queue->after:values.finalConfiguration;callbackWriterStates(cursor,action,desired,n,forward);forward.push_back({cursor,n+1});}
  std::optional<unsigned> matched;for(const auto& cut:forward){if(cut.files==actual&&(!done||cut.next==actions.size())){matched=std::max(matched.value_or(0),cut.next);}}
  if(stopped&&!done){for(const auto& cut:forward){auto rollback=cut.files;std::vector<CallbackCut> cuts{{rollback,0}};if(queue){callbackWriterStates(rollback,CallbackAction::Queue,queue->before,0,cuts);}callbackWriterStates(rollback,CallbackAction::Association,beforeConfig,0,cuts);callbackWriterStates(rollback,CallbackAction::Read,beforeResults,0,cuts);callbackWriterStates(rollback,CallbackAction::DownloadState,stateBase,0,cuts);if(std::ranges::any_of(cuts,[&](const auto& entry){return entry.files==actual;})){matched=0;break;}}}
  require(matched.has_value(),storage::ErrorCode::Conflict,"Files are not a reachable owned V2 state/association/read tuple");return *matched;
}
