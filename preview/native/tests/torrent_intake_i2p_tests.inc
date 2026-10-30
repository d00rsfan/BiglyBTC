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
  for(bool denied:{false,true}){
    Area area;auto input=parse(original);auto attrs=parse(rawValue(input,"attributes"));
    check(attrs.replaceValue(denied?"peersourcesdenied":"peersources",denied?"l7:Trackere":"l3:DHTe").has_value(),"Own disabled inherited Tracker source");
    check(input.replaceValue("attributes",attrs.serialize()).has_value(),"Own inherited peer-source metadata");write(area.input,input.serialize());
    const auto source=take(storage::inspectTorrentIntake(area.input));check(!importI2pFiles(area,source),"Explicit disabled or denied Tracker cannot be re-enabled by import context");area.untouched();
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
  for(int kind=0;kind<3;++kind){
    Area area;const auto source=take(storage::inspectTorrentIntake(area.input));
    const auto result=storage::importTorrentWithFileOptions(*area.lease,source,area.destination,true,mixed,nullptr,false,{}, {},
        false,kind==0,false,{}, {},kind==1,{},core::ExistingVerificationGoal::FullTorrent,
        kind==2?core::TorrentAllocationMode::Stopped:core::TorrentAllocationMode::None,Network::I2p);
    check(!result,"I2P context cannot imply unsupported queue, verified-seeding or allocation ownership");area.untouched();
  }
  for(int kind=0;kind<2;++kind){
    Area area;const auto result=storage::importTorrent(*area.lease,area.input,area.destination,true,nullptr,kind==0,
        kind==1?static_cast<Network>(99):Network::I2p);
    check(!result,"I2P context refuses force or invalid enum before effects");area.untouched();
  }
}
}
