// Included inside scheduler_service.cpp's storage namespace. The one service
// owns both permission variants and the same fixed-file CAS/journal descriptor.
namespace {
struct ImportedInventory {
  Raw raw;
  core::Document document;
  core::LibrarySnapshot library;
};
ImportedInventory readImportedInventory(const ProfileLease& lease) {
  auto raw=read(lease.directoryHandle(),"downloads.config",ConfigMaximum);
  auto file=ConfigFile::load(lease,"downloads.config");
  if(!file){throw Failure{file.error()};}
  if(file->recoverySource()!=RecoverySource::Primary||!raw.bytes||file->document().originalBytes()!=*raw.bytes){
    fail(ErrorCode::Conflict,"Imported Scheduler inventory requires a current primary source");}
  auto library=core::readLibrary(file->document());
  if(!library){fail(ErrorCode::InvalidFormat,"Invalid imported Scheduler inventory");}
  for(const auto& issue:library->issues){
    if(issue.pauseEntry||!library->legacyMap){fail(ErrorCode::InvalidFormat,"Malformed imported Scheduler inventory");}
    const auto& node=file->document().root().children.at(issue.sourceIndex*2+1);
    if(node.find("torrent")||node.find("torrent_hash")||node.find("save_dir")||node.find("path")){
      fail(ErrorCode::InvalidFormat,"Malformed imported legacy download");}
  }
  std::set<std::string> hashes;
  for(const auto& row:library->downloads){if(row.hash&&!hashes.insert(row.hash->hex()).second){fail(ErrorCode::Conflict,"Ambiguous imported download hash");}}
  hashes.clear();
  for(const auto& pause:library->paused){if(!hashes.insert(pause.hash.hex()).second){fail(ErrorCode::Conflict,"Ambiguous imported pause hash");}}
  return {std::move(raw),std::move(file->document()),std::move(*library)};
}
std::string importedIntentText(const ImportedSchedulerAction& action) {
  return "BiglyBTC Scheduler imported intent 1\n"+action.target.hash.hex()+"\n"
      +std::to_string(action.target.libraryRevision)+"\n"+std::to_string(action.target.manualRevision)+"\n"
      +std::to_string(static_cast<int>(action.kind))+"\n";
}
std::string importedInventoryMask(const core::Document& document,const core::TorrentHash& hash) {
  const auto parsed=core::readLibrary(document);
  if(!parsed){fail(ErrorCode::InvalidFormat,"Cannot validate imported Scheduler publication");}
  const core::Node* selected=nullptr;
  for(const auto& row:parsed->downloads){if(row.hash&&*row.hash==hash){
    if(selected){fail(ErrorCode::Conflict,"Duplicate imported publication target");}
    selected=parsed->legacyMap?&document.root().children.at(row.sourceIndex*2+1)
        :&document.find("downloads")->children.at(row.sourceIndex);
  }}
  if(!selected){fail(ErrorCode::Conflict,"Imported publication target disappeared");}
  const auto raw=[&](const core::Node& node){return document.originalBytes().substr(node.begin,node.end-node.begin);};
  const auto row=[&](const core::Node& node){
    std::string out="d";
    for(std::size_t i=0;i<node.children.size();i+=2){
      const auto key=node.children[i].bytes;
      if(key=="state"||key=="stopped"||key=="forceStart"||key=="startStopLocked"){continue;}
      out+=raw(node.children[i]);out+=raw(node.children[i+1]);
    }
    return out+"e";
  };
  std::string out="d";
  for(std::size_t i=0;i<document.root().children.size();i+=2){
    const auto& key=document.root().children[i];const auto& value=document.root().children[i+1];out+=raw(key);
    if(key.bytes=="downloads"&&value.type==core::Node::Type::List){
      out+='l';for(const auto& item:value.children){out+=&item==selected?row(item):raw(item);}out+='e';
    }else if(&value==selected){out+=row(value);}
    else if(key.bytes=="pause_data"){
      if(value.type!=core::Node::Type::List){fail(ErrorCode::InvalidFormat,"Malformed imported publication pauses");}
      out+='l';for(const auto& pause:value.children){
        const auto* identity=pause.type==core::Node::Type::Bytes?&pause:pause.find("hash");
        if(!identity||identity->type!=core::Node::Type::Bytes){fail(ErrorCode::InvalidFormat,"Malformed imported publication pause");}
        const auto decoded=core::TorrentHash::parse(identity->bytes);
        if(!decoded){fail(ErrorCode::InvalidFormat,"Malformed imported publication hash");}
        if(*decoded!=hash){out+=raw(pause);}
      }out+='e';
    }else{out+=raw(value);}
  }
  return out+"e";
}
}

std::expected<ImportedSchedulerPlan,Error> SchedulerService::evaluateImported(const ProfileLease& lease,
    scheduler::LocalMinute minute,std::span<const ImportedSchedulerTarget> targets) {
  return guarded([&]()->std::expected<ImportedSchedulerPlan,Error>{
    auto& p=*impl_;p.held(lease);
    if(p.pending||p.importedPending||p.snapshot.interruptedAction||p.importedRevocationLost){fail(ErrorCode::Busy,"Unsettled Scheduler action blocks imported permissions");}
    if(targets.size()>scheduler::MaximumTorrents){fail(ErrorCode::InvalidFormat,"Too many imported Scheduler targets");}
    auto inventory=readImportedInventory(lease);
    auto owned=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);
    if(!owned){fail(ErrorCode::InvalidFormat,"Invalid imported Scheduler ownership");}
    std::map<std::string,const core::SavedDownload*> rows;
    std::set<std::string> pauses,unique;
    for(const auto& row:inventory.library.downloads){if(row.hash){rows.emplace(row.hash->hex(),&row);}}
    for(const auto& pause:inventory.library.paused){pauses.insert(pause.hash.hex());}
    const scheduler::Configuration empty;
    if(p.snapshot.settings.enabled&&!p.snapshot.configuration){fail(ErrorCode::Missing,"No original Scheduler schedule");}
    const auto& configuration=p.snapshot.configuration?*p.snapshot.configuration:empty;safeConfiguration(configuration);
    const bool filters=p.snapshot.settings.enabled&&std::ranges::any_of(configuration.schedules,[](const auto& rule){return rule.enabled&&(rule.onlyCategory||rule.excludeCategory);});
    std::map<std::string,const ImportedSchedulerTarget*> byHash;
    std::vector<scheduler::Torrent> input;
    std::size_t unavailable=0;
    for(const auto& target:targets){
      const auto key=target.hash.hex();const auto state=static_cast<int>(target.state);const auto row=rows.find(key);
      if(!target.libraryRevision||(target.hash.bytes().size()!=20&&target.hash.bytes().size()!=32)
          ||state<1||state>9||!unique.insert(key).second||row==rows.end()||!row->second->persistent
          ||target.paused!=pauses.contains(key)||target.forceStarted!=row->second->forceStart){
        fail(ErrorCode::Conflict,"Imported Scheduler target/source identity is stale");}
      const auto present=owned->contains(hashBytes(target.hash));if(!present){fail(ErrorCode::InvalidFormat,"Invalid imported Scheduler hash");}
      // Never upgrade or replace NativeCurrent acknowledgement with an import.
      if(!*present||p.importedRevoked.contains(key)||p.authority.contains(key)){continue;}
      if(p.snapshot.settings.enabled&&!target.complete){++unavailable;continue;}
      if(filters&&!target.membershipsAvailable){fail(ErrorCode::Unsupported,"Imported filtered schedule needs actual membership");}
      byHash.emplace(key,&target);
      input.push_back({key,target.complete.value_or(false),target.state,target.forceStarted,target.paused,true,target.memberships});
    }
    const auto tick=scheduler::evaluate(configuration,minute,input,{},p.snapshot.settings.enabled,p.snapshot.settings.overrideForced);
    if(!tick){fail(ErrorCode::InvalidFormat,"Imported Scheduler policy input is unavailable");}
    if(p.planSequence==UINT64_MAX){fail(ErrorCode::Conflict,"Imported Scheduler plan sequence exhausted");}
    ImportedSchedulerPlan plan;plan.id=++p.planSequence;plan.revision=p.snapshot.revision;plan.unavailableTargets=unavailable;
    for(const auto& action:tick->actions){
      if(action.kind==scheduler::ActionKind::Pause){continue;}
      const auto found=byHash.find(action.id);if(found==byHash.end()){fail(ErrorCode::InvalidFormat,"Unknown imported Scheduler result");}
      plan.actions.push_back({plan.id,static_cast<std::uint64_t>(plan.actions.size()),*found->second,action.kind});
    }
    p.importedInventory=std::move(inventory.raw);p.importedIssued=plan;p.importedConsumed.clear();return plan;
  });
}
std::expected<void,Error> SchedulerService::beginImportedAction(const ProfileLease& lease,
    const ImportedSchedulerAction& action,const ImportedSchedulerTarget& current,std::stop_token stop) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);checkStop(stop);
    if(p.pending||p.importedPending||p.snapshot.interruptedAction||p.importedRevocationLost){fail(ErrorCode::Busy,"Unsettled Scheduler imported action");}
    if(!p.importedIssued||p.importedIssued->revision!=p.snapshot.revision||action.plan!=p.importedIssued->id
        ||action.ordinal>=p.importedIssued->actions.size()||p.importedIssued->actions[action.ordinal]!=action
        ||p.importedConsumed.contains(action.ordinal)||action.target!=current
        ||p.importedRevoked.contains(current.hash.hex())||p.authority.contains(current.hash.hex())){
      fail(ErrorCode::Conflict,"Imported Scheduler action/target is stale");}
    unchanged(lease.directoryHandle(),"downloads.config",p.importedInventory,ConfigMaximum);
    unchanged(lease.directoryHandle(),"biglybt.config",p.config,ConfigMaximum);
    if(p.data.get()<0){fail(ErrorCode::Conflict,"Imported ownership data disappeared");}
    unchanged(p.data.get(),XmlName,p.xml,XmlMaximum);unchanged(p.data.get(),OwnershipName,p.ownership,scheduler::OwnershipFile::MaximumBytes);
    unchanged(p.data.get(),IntentName,p.intent,IntentMaximum);checkStop(stop);
    auto retained=action; // Allocate owned membership strings BEFORE publishing intent.
    try{p.intent=replace(p.data.get(),IntentName,p.intent,importedIntentText(action),IntentMaximum);}
    catch(...){p.poisoned=true;p.warning();throw;}
    p.importedPending.emplace(std::move(retained));p.warning();return {};
  });
}
std::expected<void,Error> SchedulerService::finishImportedAction(const ProfileLease& lease,
    const ImportedSchedulerAction& action,SchedulerActionResult result,const LibraryCommandResult* publication) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);
    if(!p.importedPending||*p.importedPending!=action||!p.intent.bytes||*p.intent.bytes!=importedIntentText(action)){
      fail(ErrorCode::Conflict,"Imported Scheduler acknowledgement does not match its journal");}
    if(result==SchedulerActionResult::Indeterminate){p.poisoned=true;p.warning();fail(ErrorCode::Conflict,"Imported Scheduler outcome is uncertain; no automatic replay");}
    if(result!=SchedulerActionResult::Succeeded&&result!=SchedulerActionResult::FailedBeforeEffect){fail(ErrorCode::InvalidFormat,"Invalid imported Scheduler acknowledgement");}
    p.importedConsumed.insert(action.ordinal); // Preallocate acknowledgement state before destructive cleanup.
    try{
      if(result==SchedulerActionResult::Succeeded){
        auto after=readImportedInventory(lease);
        if(action.kind==scheduler::ActionKind::SetStopReason){
          unchanged(lease.directoryHandle(),"downloads.config",p.importedInventory,ConfigMaximum);
        }else{
          const auto before=core::Document::parse(*p.importedInventory.bytes,compatibleConfigLimits());
          const auto wanted=action.kind==scheduler::ActionKind::Resume?core::DownloadAction::Resume:core::DownloadAction::Start;
          if(!before){fail(ErrorCode::InvalidFormat,"Invalid imported publication beforeimage");}
          const auto beforeRows=core::readLibrary(*before);
          const auto matches=[](const core::SavedDownload& a,const core::SavedDownload& b){
            return a.hash==b.hash&&a.originalState==b.originalState&&a.startup==b.startup&&a.forceStart==b.forceStart;
          };
          const core::SavedDownload* prior=nullptr;const core::SavedDownload* next=nullptr;
          const core::SavedPause* priorPause=nullptr;
          if(beforeRows){
            for(const auto& row:beforeRows->downloads){if(row.hash&&*row.hash==action.target.hash){prior=&row;}}
            for(const auto& pause:beforeRows->paused){if(pause.hash==action.target.hash){priorPause=&pause;}}
          }
          for(const auto& row:after.library.downloads){if(row.hash&&*row.hash==action.target.hash){next=&row;}}
          const bool samePause=publication&&publication->pauseBefore.has_value()==(priorPause!=nullptr)
              &&(!priorPause||(publication->pauseBefore->hash==priorPause->hash
                  &&publication->pauseBefore->forceStart==priorPause->forceStart&&publication->pauseBefore->owner==priorPause->owner));
          if(!before||!beforeRows||!publication||publication->action!=wanted||publication->source!=RecoverySource::Primary
              ||!prior||!next||!samePause
              ||!matches(*prior,publication->before)||!matches(*next,publication->after)||publication->pauseAfter
              ||importedInventoryMask(*before,action.target.hash)!=importedInventoryMask(after.document,action.target.hash)
              ||std::ranges::any_of(after.library.paused,[&](const auto& pause){return pause.hash==action.target.hash;})){
            fail(ErrorCode::Conflict,"Imported Scheduler publication changed unapproved library scope");}
          auto file=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);if(!file){fail(ErrorCode::InvalidFormat,"Invalid imported Scheduler pause source");}
          const auto bytes=file->afterResume(hashBytes(action.target.hash));if(!bytes){fail(ErrorCode::InvalidFormat,"Cannot consume original imported permission");}
          if(*bytes!=p.snapshot.ownershipBytes){p.ownership=replace(p.data.get(),OwnershipName,p.ownership,*bytes,scheduler::OwnershipFile::MaximumBytes);p.snapshot.ownershipBytes=*bytes;}
        }
        p.importedInventory=std::move(after.raw);
      }else{unchanged(lease.directoryHandle(),"downloads.config",p.importedInventory,ConfigMaximum);}
      remove(p.data.get(),IntentName,p.intent,IntentMaximum);p.intent={};
    }catch(...){p.poisoned=true;p.warning();throw;}
    p.importedPending.reset();
    if(p.snapshot.ownershipBytes.empty()){p.inheritedRecords=false;}
    p.snapshot.diagnostic.clear();p.warning();return {};
  });
}
