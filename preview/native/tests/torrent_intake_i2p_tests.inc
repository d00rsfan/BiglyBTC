// Included immediately before main() in torrent_intake_file_options_tests.cpp.
// Call i2pNetworkContexts() once from its existing try block. No new test owner.
namespace {
std::expected<core::TorrentHash,storage::Error> importI2pFiles(Area& area,
    const storage::TorrentIntakeMetadata& source,void(*observer)(storage::TorrentIntakeStage)=nullptr,
    bool paused=false,std::span<const compat::FilePolicy> policies=mixed){
  return storage::importTorrentWithFileOptions(*area.lease,source,area.destination,true,policies,
      observer,false,{}, {},false,false,paused,{}, {},false,{},
      core::ExistingVerificationGoal::FullTorrent,core::TorrentAllocationMode::None,storage::TorrentIntakeNetwork::I2p);
}
void checkI2pState(Area& area,const core::TorrentHash& hash){
  const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));
  const auto* attributes=state.find("attributes");const auto* networks=attributes->find("networks");
  check(networks&&networks->type==core::Node::Type::List&&networks->children.size()==1
      &&networks->children[0].type==core::Node::Type::Bytes&&networks->children[0].bytes=="I2P","Exact saved I2P-only context");
  const auto* peers=attributes->find("peersources");check(peers&&peers->type==core::Node::Type::List
      &&std::ranges::any_of(peers->children,[](const auto& value){return value.type==core::Node::Type::Bytes&&value.bytes=="Tracker";}),"Real ProfileSource Tracker prerequisite persisted");
  check(rawValue(state,"info")==rawValue(parse(read(area.input)),"info"),"I2P context does not rewrite raw info");
  check(!fs::exists(area.destination),"Network context grants no payload allocation");
}
void i2pNetworkContexts(){
  using Network=storage::TorrentIntakeNetwork;
  const std::string tracker="http://"+std::string(52,'a')+".b32.i2p/announce";
  const auto original=withTrackers(tracker,{{tracker}});
  std::string defaultPublicState;
  for(bool explicitPublic:{false,true}){
    Area area;write(area.input,original);
    const auto hash=explicitPublic
        ?take(storage::importTorrent(*area.lease,area.input,area.destination,true,nullptr,false,Network::Public))
        :take(storage::importTorrent(*area.lease,area.input,area.destination,true));
    const auto bytes=read(area.profile/"active"/(hash.hex()+".dat"));
    if(explicitPublic){check(bytes==defaultPublicState,"Explicit Public preserves byte-exact default state");}else{defaultPublicState=bytes;}
    const auto state=parse(bytes);const auto* networks=state.find("attributes")->find("networks");
    check(networks&&networks->children.size()==1&&networks->children[0].bytes=="Public","Old default remains Public despite tracker hostname");
    check(read(area.input)==original&&read(area.profile/"torrents"/(hash.hex()+".torrent"))==original,"Public archive/source bytes unchanged");
  }
  for(bool stopped:{true,false}){
    Area area;write(area.input,original);const auto hash=take(storage::importTorrent(*area.lease,area.input,area.destination,stopped,nullptr,false,Network::I2p));
    checkI2pState(area,hash);const auto library=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(library&&library->downloads.size()==1
        &&library->downloads[0].startup==(stopped?core::StartupState::Stopped:core::StartupState::Waiting)
        &&!library->downloads[0].forceStart,"Simple I2P import preserves explicit unforced start/stop intent");
    check(read(area.input)==original&&read(area.profile/"torrents"/(hash.hex()+".torrent"))==original,"I2P archive/input remain exact originals");
  }
  {
    Area area;write(area.input,original);const auto source=take(storage::inspectTorrentIntake(area.input));
    const std::vector<compat::FilePolicy> skipped(4,{true,0});
    const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,source,area.destination,false,skipped,nullptr,false,{}, {},
        false,false,false,{}, {},false,{},core::ExistingVerificationGoal::FullTorrent,core::TorrentAllocationMode::None,Network::I2p));
    verify(area,hash,"CCCC",skipped);checkI2pState(area,hash);
    const auto saved=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(saved&&saved->downloads[0].startup==core::StartupState::Stopped&&!saved->downloads[0].forceStart
        &&saved->paused.empty(),"All-skipped I2P start normalizes to stopped without payload or pause authority");
  }
  for(auto boundary:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,
      storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;write(area.input,original);const auto source=take(storage::inspectTorrentIntake(area.input));interrupted=boundary;
    check(!importI2pFiles(area,source,interrupt,true),"Interrupt accepted I2P transaction at actual durable boundary");
    const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));
    check(journal.find("network")&&journal.find("network")->bytes=="I2P","Accepted I2P context is durable, never inferred during recovery");
    area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash&&*hash==source.hash(),"Recover same exact I2P identity");
    verify(area,*hash,"LCLC",mixed);checkI2pState(area,*hash);
    const auto saved=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(saved&&saved->paused.size()==1&&saved->paused[0].hash==*hash,"Original pause marker survives I2P transaction recovery");
    check(read(area.input)==original&&read(area.profile/"torrents"/(hash->hex()+".torrent"))==original,"Recovery preserves exact archive/source bytes");
  }
  for(const auto* network:{"l6:Publice","l3:Tore","l3:I2P6:Publice","le","3:I2P"}){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    check(attrs.replaceValue("networks",network).has_value()&&input.replaceValue("attributes",attrs.serialize()).has_value(),"Own incompatible inherited network");
    const auto bytes=input.serialize();write(area.input,bytes);const auto source=take(storage::inspectTorrentIntake(area.input));
    check(!importI2pFiles(area,source),"I2P context cannot silently reinterpret inherited networks");area.untouched();check(read(area.input)==bytes,"Incompatible source remains unchanged");
  }
  for(int source=0;source<3;++source){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    if(source!=1){check(attrs.replaceValue("peersources","l3:DHTe").has_value(),"Own explicit DHT-only source");}
    if(source){check(attrs.replaceValue("peersourcesdenied",source==1?"l7:Trackere":"l3:DHTe").has_value(),"Own denied actual source");}
    check(input.replaceValue("attributes",attrs.serialize()).has_value(),"Own inherited peer-source metadata");const auto bytes=input.serialize();write(area.input,bytes);
    const auto metadata=take(storage::inspectTorrentIntake(area.input));const auto result=importI2pFiles(area,metadata);
    if(source){check(!result,"Denied default Tracker or explicit DHT cannot be re-enabled by intake");area.untouched();}
    else{check(result.has_value(),"Explicit inherited DHT-only selection remains supported");const auto state=parse(read(area.profile/"active"/(result->hex()+".dat")));check(rawValue(parse(rawValue(state,"attributes")),"peersources")=="l3:DHTe","Explicit DHT-only selection preserved");}
    check(read(area.input)==bytes,"Peer-source selection never rewrites input");
  }
  {
    Area area;write(area.input,original);const auto hash=take(storage::importTorrent(*area.lease,area.input,area.destination,true));
    const auto inventory=read(area.profile/"downloads.config"),state=read(area.profile/"active"/(hash.hex()+".dat"));
    const auto source=take(storage::inspectTorrentIntake(area.input));const auto result=importI2pFiles(area,source);
    check(!result&&result.error().code==storage::ErrorCode::Conflict,"Existing saved Public identity cannot be converted by I2P re-import");
    check(read(area.profile/"downloads.config")==inventory&&read(area.profile/"active"/(hash.hex()+".dat"))==state
        &&!storage::hasPendingTorrentIntake(*area.lease),"Existing Public row/state remain byte-exact without a new journal");
  }
  {
    Area area;const auto source=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
    check(!importI2pFiles(area,source,interrupt),"Freeze accepted I2P journal");auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));
    check(journal.replaceValue("network",b("Public")).has_value(),"Own invalid accepted network marker");const auto bytes=journal.serialize();write(area.profile/"BiglyBTC.torrent-import",bytes);
    const auto inventory=read(area.profile/"downloads.config");const auto refused=storage::recoverTorrentIntake(*area.lease);
    check(!refused&&refused.error().code==storage::ErrorCode::InvalidFormat,"Invalid accepted network cannot fall back to Public");
    check(read(area.profile/"BiglyBTC.torrent-import")==bytes&&read(area.profile/"downloads.config")==inventory
        &&!fs::exists(area.profile/"active")&&!fs::exists(area.destination),"Malformed recovery preserves pending proof and all original bytes");
  }
  for(bool interruptedQueued:{false,true}){
    Area area;write(area.input,original);const auto source=take(storage::inspectTorrentIntake(area.input));
    interrupted=storage::TorrentIntakeStage::JournalSynced;
    const auto result=storage::importTorrentWithFileOptions(*area.lease,source,area.destination,false,mixed,
        interruptedQueued?interrupt:nullptr,false,{}, {8192,4096},false,true,false,{}, {},false,{},
        core::ExistingVerificationGoal::FullTorrent,core::TorrentAllocationMode::None,Network::I2p);
    if(interruptedQueued){check(!result,"Interrupt queued I2P before inventory publication");area.reopen();check(take(storage::recoverTorrentIntake(*area.lease)).has_value(),"Recover queued I2P context");}
    else{check(result.has_value(),"I2P accepts ordinary queued intent");}
    const auto library=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(library&&library->downloads.size()==1&&library->downloads[0].startup==core::StartupState::Queued
        &&!library->downloads[0].forceStart&&library->paused.empty(),"Queued75 remains distinct from stopped70 and paused");
    checkI2pState(area,source.hash());
  }
  for(int kind=0;kind<3;++kind){
    Area area;const auto source=take(storage::inspectTorrentIntake(area.input));
    const auto result=storage::importTorrentWithFileOptions(*area.lease,source,area.destination,true,mixed,nullptr,false,{}, {},
        false,kind==0,false,{}, {},kind==1,{},core::ExistingVerificationGoal::FullTorrent,
        kind==2?core::TorrentAllocationMode::Stopped:core::TorrentAllocationMode::None,Network::I2p);
    check(!result,"I2P context cannot imply unsupported queue, verified-seeding or allocation ownership");area.untouched();
  }
  {
    Area area;const auto result=storage::importTorrent(*area.lease,area.input,area.destination,true,nullptr,false,static_cast<Network>(99));
    check(!result,"Invalid I2P context enum refuses before effects");area.untouched();
  }
  for(auto boundary:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,
      storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;write(area.input,original);interrupted=boundary;
    check(!storage::importTorrent(*area.lease,area.input,area.destination,false,interrupt,true,Network::I2p),"Interrupt forced I2P import at real durable boundary");
    const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(journal.find("forceStart")->integer==1&&journal.find("network")->bytes=="I2P","Original forced intent and I2P context share the same journal");
    area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash.has_value(),"Recover actual forced I2P intake");checkI2pState(area,*hash);
    const auto saved=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(saved&&saved->downloads.size()==1&&saved->downloads[0].forceStart&&saved->downloads[0].startup==core::StartupState::Waiting&&saved->paused.empty(),"Forced Waiting0 survives each exact I2P recovery boundary without payload ownership");
  }

}
}

namespace {
std::expected<core::TorrentHash,storage::Error> importMixedFiles(Area& area,
    const storage::TorrentIntakeMetadata& source,void(*observer)(storage::TorrentIntakeStage)=nullptr,
    bool force=false,bool paused=false,bool queued=false,bool verify=false,
    core::TorrentAllocationMode allocation=core::TorrentAllocationMode::None){
  return storage::importTorrentWithFileOptions(*area.lease,source,area.destination,!queued,mixed,
      observer,force,{}, {8192,4096},false,queued,paused,{}, {},verify,{},
      core::ExistingVerificationGoal::FullTorrent,allocation,storage::TorrentIntakeNetwork::Mixed);
}
void checkMixedState(Area& area,const core::TorrentHash& hash,std::string_view expected="l6:Public3:I2Pe"){
  const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));
  const auto attrs=parse(rawValue(state,"attributes"));
  check(rawValue(attrs,"networks")==expected,"Exact original mixed network order/encoding survives");
  const auto* peers=attrs.find("peersources");check(peers&&peers->type==core::Node::Type::List
      &&std::ranges::any_of(peers->children,[](const auto& value){return value.type==core::Node::Type::Bytes&&value.bytes=="Tracker";}),"Mixed import retains real Tracker permission");
  check(rawValue(state,"info")==rawValue(parse(read(area.input)),"info"),"Mixed import preserves raw info");
  check(!fs::exists(area.destination),"Mixed context grants no payload allocation");
}
void mixedNetworkContexts(){
  const std::string publicTracker="http://127.0.0.1:9/announce",i2pTracker="http://"+std::string(52,'a')+".b32.i2p/announce";
  const auto original=withTrackers(publicTracker,{{publicTracker},{i2pTracker}});
  for(int intent=0;intent<4;++intent){
    Area area;write(area.input,original);const auto source=take(storage::inspectTorrentIntake(area.input));
    const auto hash=take(importMixedFiles(area,source,nullptr,intent==3,intent==2,intent==1));
    verify(area,hash,"LCLC",mixed);checkMixedState(area,hash);
    const auto saved=core::readLibrary(parse(read(area.profile/"downloads.config")));check(saved&&saved->downloads.size()==1,"One original mixed row");
    check(saved->downloads[0].startup==(intent==3?core::StartupState::Waiting:intent==1?core::StartupState::Queued:core::StartupState::Stopped)
        &&saved->downloads[0].forceStart==(intent==3)&&saved->paused.size()==static_cast<std::size_t>(intent==2),"Mixed ordinary start/queue/pause/force intent is distinct");
    const auto inventory=parse(read(area.profile/"downloads.config"));const auto& row=inventory.find("downloads")->children.at(0);
    check(row.find("maxdl")->integer==8192&&row.find("maxul")->integer==4096,"Mixed limits remain original payload rates");
    check(read(area.input)==original&&read(area.profile/"torrents"/(hash.hex()+".torrent"))==original,"Mixed source/archive remain byte exact");
  }
  for(auto boundary:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,
      storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;write(area.input,original);const auto source=take(storage::inspectTorrentIntake(area.input));interrupted=boundary;
    check(!importMixedFiles(area,source,interrupt,false,true),"Interrupt accepted mixed commit at the actual boundary");
    const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(journal.find("network")&&journal.find("network")->bytes=="Public+I2P","Accepted mixed context is explicit and durable");
    area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash&&*hash==source.hash(),"Mixed recovery retains selected identity");
    verify(area,*hash,"LCLC",mixed);checkMixedState(area,*hash);const auto saved=core::readLibrary(parse(read(area.profile/"downloads.config")));
    check(saved&&saved->paused.size()==1&&saved->paused[0].hash==*hash&&!saved->downloads[0].forceStart,"Mixed recovery preserves pause intent in the same transaction");
  }
  for(const auto* selection:{"l6:Public3:I2Pe","l3:I2P6:Publice"}){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    check(attrs.replaceValue("networks",selection).has_value()&&input.replaceValue("attributes",attrs.serialize()).has_value(),"Own exact inherited mixed selection");
    const auto bytes=input.serialize();write(area.input,bytes);const auto source=take(storage::inspectTorrentIntake(area.input));const auto hash=take(importMixedFiles(area,source));
    verify(area,hash,"LCLC",mixed);checkMixedState(area,hash,selection);check(read(area.input)==bytes,"Inherited source order is not rewritten");
  }
  for(const auto* selection:{"l6:Publice","l3:I2Pe","l3:Tore","l6:Public6:Publice","l6:Public3:I2P3:Tore","le","3:I2P"}){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    check(attrs.replaceValue("networks",selection).has_value()&&input.replaceValue("attributes",attrs.serialize()).has_value(),"Own incompatible mixed input");
    const auto bytes=input.serialize();write(area.input,bytes);const auto source=take(storage::inspectTorrentIntake(area.input));
    check(!importMixedFiles(area,source),"Mixed import cannot broaden or normalize an existing network selection");area.untouched();check(read(area.input)==bytes,"Rejected network input remains exact");
  }
  for(int source=0;source<3;++source){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    if(source!=1){check(attrs.replaceValue("peersources","l3:DHTe").has_value(),"Own mixed DHT-only source");}
    if(source){check(attrs.replaceValue("peersourcesdenied",source==1?"l7:Trackere":"l3:DHTe").has_value(),"Own denied mixed source");}
    check(input.replaceValue("attributes",attrs.serialize()).has_value(),"Own mixed peer sources");const auto bytes=input.serialize();write(area.input,bytes);
    const auto metadata=take(storage::inspectTorrentIntake(area.input));const auto result=importMixedFiles(area,metadata);
    if(source){check(!result,"Mixed choice cannot override denied actual source");area.untouched();}
    else{check(result.has_value(),"Mixed import preserves explicit DHT-only source");const auto state=parse(read(area.profile/"active"/(result->hex()+".dat")));check(rawValue(parse(rawValue(state,"attributes")),"peersources")=="l3:DHTe","Mixed DHT-only selection remains exact");}
    check(read(area.input)==bytes,"Mixed source bytes remain exact");
  }
  for(const auto* value:{"6:Public","5:Mixed","10:Public+I2p","i2e"}){
    Area area;const auto source=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
    check(!importMixedFiles(area,source,interrupt),"Own accepted mixed journal");auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));
    check(journal.replaceValue("network",value).has_value(),"Own malformed accepted context");const auto bytes=journal.serialize();write(area.profile/"BiglyBTC.torrent-import",bytes);
    const auto before=read(area.profile/"downloads.config");area.reopen();const auto result=storage::recoverTorrentIntake(*area.lease);
    check(!result&&result.error().code==storage::ErrorCode::InvalidFormat,"Unknown mixed journal values never fall back");
    check(read(area.profile/"downloads.config")==before&&read(area.profile/"BiglyBTC.torrent-import")==bytes&&!fs::exists(area.profile/"active"),"Malformed recovery preserves proof without effects");
  }
  for(bool allocation:{false,true}){
    Area area;const auto source=take(storage::inspectTorrentIntake(area.input));
    check(!importMixedFiles(area,source,nullptr,false,false,false,!allocation,allocation?core::TorrentAllocationMode::Stopped:core::TorrentAllocationMode::None),"Mixed verification/allocation are refused before effects");area.untouched();
  }
  {
    Area area;write(area.input,original);const auto hash=take(storage::importTorrent(*area.lease,area.input,area.destination,true));
    const auto before=read(area.profile/"downloads.config"),state=read(area.profile/"active"/(hash.hex()+".dat"));const auto source=take(storage::inspectTorrentIntake(area.input));
    check(!importMixedFiles(area,source)&&read(area.profile/"downloads.config")==before&&read(area.profile/"active"/(hash.hex()+".dat"))==state
        &&!storage::hasPendingTorrentIntake(*area.lease),"Mixed re-import cannot convert an existing saved Public row");
  }
}
}
