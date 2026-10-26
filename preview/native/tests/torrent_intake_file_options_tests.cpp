#include "torrent_intake.hpp"
#include "verified_seeding.hpp"
#include "library_commands.hpp"
#include "biglybtc/saved_dnd.hpp"
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <unistd.h>

using namespace biglybtc;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,storage::Error> value){check(value.has_value(),value?"":value.error().message.c_str());return std::move(*value);}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Open owned input");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(value.data(),static_cast<std::streamsize>(value.size()));check(bool(out),"Write owned input");}
core::Document parse(std::string value){auto result=core::Document::parse(std::move(value),storage::compatibleConfigLimits());check(result.has_value(),"Parse owned document");return std::move(*result);}
std::string metadata(){
  std::string files="l";
  for(const auto& [name,length]:std::array<std::pair<std::string_view,int>,3>{{{"first.bin",100},{"skip.bin",200},{"last.bin",300}}}){
    files+="d6:lengthi"+std::to_string(length)+"e4:pathl"+b(name)+"ee";
  }
  files+="d4:attr1:p6:lengthi0e4:pathl4:.pad1:0eee";
  return "d10:attributesd6:future3:raw10:parametersd6:futurei7eee4:infod5:files"+files+
      "4:name6:bundle12:piece lengthi16384e6:pieces"+b(std::string(20,'h'))+"ee";
}
std::string metadataWithFlags(std::string encoded) {
  auto source=parse(metadata());auto attrs=parse("d6:future3:raw10:parametersd6:futurei7ee5:flags"+encoded+'e');
  check(source.replaceValue("attributes",attrs.serialize()).has_value(),"Own original flag metadata");return source.serialize();
}
std::string pauseBytes(const core::Document& document){
  const auto* value=document.find("pause_data");check(value,"Saved original pause list exists");
  return document.originalBytes().substr(value->begin,value->end-value->begin);
}
struct Area {
  fs::path root,profile,input,destination;std::optional<storage::ProfileLease> lease;
  Area(){
    char name[]="/tmp/biglybtc-intake-file-options-XXXXXX";auto* made=::mkdtemp(name);check(made,"Owned root");
    root=made;profile=root/"profile";input=root/"input.torrent";destination=root/"new/payload";
    fs::create_directories(profile);write(input,metadata());write(profile/"downloads.config","d9:downloadsle6:futurei77ee");
    acquire();
  }
  ~Area(){lease.reset();std::error_code ignored;fs::remove_all(root,ignored);}
  void acquire(){lease.emplace(take(storage::ProfileLease::acquire(profile)));}
  void reopen(){lease.reset();acquire();}
  void untouched()const{
    check(!fs::exists(profile/"active")&&!fs::exists(profile/"torrents")&&!storage::hasPendingTorrentIntake(*lease),"No profile artifacts");
    check(read(profile/"downloads.config")=="d9:downloadsle6:futurei77ee","Unknown inventory preserved");
  }
};
std::vector<compat::FilePolicy> mixed{{false,1},{true,0},{false,-1},{true,0}};
storage::TorrentIntakeStage interrupted=storage::TorrentIntakeStage::JournalSynced;
void interrupt(storage::TorrentIntakeStage stage){if(stage==interrupted){throw std::runtime_error("Owned accepted-commit interruption");}}
void verify(Area& area,const core::TorrentHash& hash,std::string_view kinds,std::span<const compat::FilePolicy> expected){
  const auto inventory=parse(read(area.profile/"downloads.config"));
  check(inventory.find("future")->integer==77,"Inventory unknown survives");
  const auto& row=inventory.find("downloads")->children.at(0);
  const auto actual=compat::originalFilePolicy(row,4);check(actual&&std::ranges::equal(*actual,expected),"Actual shared original policy decoder");
  const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));const auto* attrs=state.find("attributes");
  check(attrs&&attrs->find("future")->bytes=="raw"&&attrs->find("parameters")->find("future")->integer==7,"Metadata unknowns survive");
  const auto& stores=attrs->find("storetypes")->children;check(stores.size()==4,"Exact original indices");
  for(std::size_t n=0;n<stores.size();++n){check(stores[n].bytes==kinds.substr(n,1),"Frozen original storage policy");}
  const auto facts=core::readSavedDndFacts(attrs);
  const auto selected=core::projectSavedDndProgress(600,0,facts);
  std::int64_t skipped=0;for(std::size_t n=0;n<4;++n){if(expected[n].skipped){skipped+=std::array{100,200,300,0}[n];}}
  check(selected.selectedBytes==600-skipped&&selected.selectedRemaining==600-skipped,"Exact saved selected size/remaining");
  check(selected.percentThousandths==(skipped==600?1000:0),"All-skipped is selected-complete only");
  check(facts.skippedDownloadedBytes==0,"No unverified existing data credited");
  check(row.find("completedbytes")->integer==0,"No invented verified torrent bytes");
  check(!storage::hasPendingTorrentIntake(*area.lease),"Commit journal retired");
}
}

namespace {
std::string encodedTiers(const core::TorrentIntakeTrackerTiers& tiers){
  std::string value="l";for(const auto& tier:tiers){value+='l';for(const auto& url:tier){value+=b(url);}value+='e';}return value+'e';
}
std::string withTrackers(std::string primary,const core::TorrentIntakeTrackerTiers& tiers){
  auto source=parse(metadata());check(source.replaceValue("announce",b(primary)).has_value(),"Source primary");
  check(source.replaceValue("announce-list",encodedTiers(tiers)).has_value(),"Source tiers");return source.serialize();
}
std::string rawValue(const core::Document& source,const char* key){const auto* node=source.find(key);check(node,"Raw field present");return source.originalBytes().substr(node->begin,node->end-node->begin);}
void trackerOptions(){
  using Tiers=core::TorrentIntakeTrackerTiers;
  const std::string keep="http://keep.invalid:81/announce",x="https://other.invalid/announce",y="udp://127.0.0.1:9/announce";
  const auto source=withTrackers(keep,{{keep},{y}});const auto original=parse(source);
  // Previously accepted metadata must retain the old no-edit path even when a
  // bounded ASCII editor cannot represent its exact original tracker fields.
  for(int kind=0;kind<7;++kind){
    Area area;auto input=parse(source);
    if(kind==0){check(input.replaceValue("announce-list",encodedTiers(Tiers(17,{x}))).has_value(),"Large original tier set");}
    if(kind==1){check(input.replaceValue("announce-list",encodedTiers(Tiers{std::vector<std::string>(65,x)})).has_value(),"Large original URL count");}
    if(kind==2){check(input.replaceValue("announce",b("https://original.invalid/"+std::string(2049,'x'))).has_value(),"Long original primary URL");}
    if(kind==3){check(input.replaceValue("announce-list",encodedTiers({{"https://original.invalid/\xd1\x8f"}})).has_value(),"Unicode original URL bytes");}
    if(kind==4){check(input.replaceValue("announce-list","3:raw").has_value(),"Ignored opaque original tracker-list type");}
    if(kind==5){check(input.replaceValue("announce-list",encodedTiers(Tiers{std::vector<std::string>(64,"https://original.invalid/"+std::string(1500,'x'))})).has_value(),"Large original tracker byte budget");}
    if(kind==6){check(input.replaceValue("announce","i7e").has_value(),"Ignored opaque original primary type");}
    const auto bytes=input.serialize();write(area.input,bytes);const auto prepared=take(storage::inspectTorrentIntake(area.input));
    check(!prepared.trackers(),"Original projection explicitly unavailable; never silently empty/truncated");area.untouched();
    check(take(storage::rewriteTorrentIntakeTrackers(bytes,std::nullopt))==bytes,"Pure absent edit does not parse or reject old tracker fields");
    const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed));
    check(read(area.profile/"torrents"/(hash.hex()+".torrent"))==bytes&&read(area.input)==bytes,"Real untouched import preserves every original archive/source byte");
    check(!fs::exists(area.destination),"Unavailable tracker editor has no payload effects");verify(area,hash,"LCLC",mixed);
    for(const Tiers& override:std::vector<Tiers>{{},{{x},{y}}}){
      const auto replacement=take(storage::rewriteTorrentIntakeTrackers(bytes,override));const auto projected=parse(replacement);
      check(rawValue(projected,"info")==rawValue(input,"info"),"Explicit bounded backend replacement keeps raw info despite unsupported old tiers");
      check(override.empty()?!projected.find("announce-list"):rawValue(projected,"announce-list")==encodedTiers(override),"Old unrepresentable fields do not block explicit bounded clear/replacement");
    }
  }

  const auto absent=take(storage::rewriteTorrentIntakeTrackers(source,std::nullopt));check(absent==source,"Absent override preserves every source byte");
  struct Case{Tiers input;std::string primary;Tiers stored;};
  const std::vector<Case> cases{{{},"",{}},{{{},{}},"",{}},{{{x}},x,{}},{{{x,x}},x,{{x,x}}},
      {{{x},{keep,y}},keep,{{x},{keep,y}}},{{{x},{y}},x,{{x},{y}}},{{{},{x},{},{y},{}},x,{{x},{y}}}};
  for(const auto& value:cases){
    Area area;write(area.input,source);const auto prepared=take(storage::inspectTorrentIntake(area.input));
    check(prepared.trackers()==core::TorrentIntakeTrackerFields{keep,{{keep},{y}}},"Sealed preview owns exact source tracker fields");
    auto marker=prepared.hash().hex();for(auto& c:marker){if(c>='a'&&c<='f'){c=static_cast<char>(c-'a'+'A');}}
    const auto expected=value.primary.empty()?"dht://"+marker+".dht/announce":value.primary;
    const auto pure=take(storage::rewriteTorrentIntakeTrackers(source,value.input));const auto projected=parse(pure);
    check(projected.find("announce")->bytes==expected,"Original primary/singleton/selected-hash marker rule");
    check(value.stored.empty()?!projected.find("announce-list"):rawValue(projected,"announce-list")==encodedTiers(value.stored),"Original tier filtering and duplicate retention");
    const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},false,false,false,value.input));
    verify(area,hash,"LCLC",mixed);const auto archive=read(area.profile/"torrents"/(hash.hex()+".torrent"));const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));
    check(archive==pure&&read(area.input)==source,"Actual archive uses accepted edit without rewriting source");
    check(rawValue(state,"info")==rawValue(original,"info")&&rawValue(projected,"info")==rawValue(original,"info"),"Tracker editing cannot rewrite raw info");
    check(rawValue(projected,"attributes")==rawValue(original,"attributes"),"Pure edit retains unknown source subtree verbatim");
    if(value.primary.empty()){
      libtorrent::error_code error;libtorrent::torrent_info parsed(pure.data(),static_cast<int>(pure.size()),error);
      check(!error&&parsed.is_valid()&&parsed.trackers().empty(),"Saved original DHT marker is not an LT tracker request");
    }
    check(!fs::exists(area.destination),"Tracker editing has no payload effects");
  }
  {
    Area area;write(area.input,source);const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed));
    check(read(area.profile/"torrents"/(hash.hex()+".torrent"))==source&&read(area.input)==source,"Default real intake preserves source archive byte-exact");
  }
  std::vector<Tiers> bad{{{"file:///tmp/wrong"}},{{"trackerlist:http://other.invalid/a"}},{{"http://bad host/a"}},{{"udp://localhost/a"}},{{"http://localhost:0/a"}},{{"http://localhost:65536/a"}},{{"http://localhost:999999999999999999999999999999/a"}},{{"http://[broken::ip]/a"}},{{"http://-invalid/a"}},{{"http://bad..name/a"}},{{"http://localhost/a\\b"}}};
  bad.push_back(Tiers(17,{x}));bad.push_back(Tiers{std::vector<std::string>(65,x)});
  bad.push_back({{"http://localhost/"+std::string(2048,'x')}});bad.push_back(Tiers{std::vector<std::string>(64,"http://localhost/"+std::string(1500,'x'))});
  for(const auto& value:bad){Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto result=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},false,false,false,value);check(!result&&result.error().code==storage::ErrorCode::InvalidFormat,"Invalid/bounded literal tracker override refuses before any effects");area.untouched();}
  for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    for(const Tiers& tiers:std::vector<Tiers>{{},{{x},{keep,y}}}){
      Area area;write(area.input,source);const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=stage;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {},false,false,true,tiers),"Interrupt accepted paused + tracker override");
      const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(journal.find("metadata")->bytes==source,"Journal retains original source bytes, not edited archive");
      check(rawValue(parse(rawValue(journal,"fileOptions")),"trackers")==encodedTiers(tiers),"Journal retains absent-versus-empty typed tracker override");
      area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered&&*recovered==prepared.hash(),"Exact identity after tracker replay");
      check(read(area.profile/"torrents"/(recovered->hex()+".torrent"))==take(storage::rewriteTorrentIntakeTrackers(source,tiers)),"Every durable stage replays exact tracker edit");
      const auto inventory=core::readLibrary(parse(read(area.profile/"downloads.config")));check(inventory&&inventory->paused.size()==1&&inventory->paused[0].hash==*recovered,"Tracker replay preserves atomic paused intent");
      check(read(area.input)==source&&!fs::exists(area.destination),"Recovery cannot rewrite source or allocate payload");
    }
  }
  for(const auto& broken:std::vector<std::string>{"3:bad","l3:bade","ll0:ee",encodedTiers({{"file:///tmp/wrong"}})}){
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
    check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {},false,false,false,Tiers{{x}}),"Freeze accepted tracker journal");
    auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));auto options=parse(rawValue(journal,"fileOptions"));
    check(options.replaceValue("trackers",broken).has_value(),"Own malformed tracker recovery input");check(journal.replaceValue("fileOptions",options.serialize()).has_value(),"Retain other accepted journal authority");write(area.profile/"BiglyBTC.torrent-import",journal.serialize());
    const auto before=read(area.profile/"downloads.config");area.reopen();check(!storage::recoverTorrentIntake(*area.lease),"Malformed tracker replay refuses");
    check(read(area.profile/"downloads.config")==before&&!fs::exists(area.profile/"active")&&storage::hasPendingTorrentIntake(*area.lease),"Bad replay keeps intent and inventory without effects");
  }
  {
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));write(area.input,source);const auto result=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},false,false,false,Tiers{{x}});
    check(!result&&result.error().code==storage::ErrorCode::Conflict,"Tracker edits never bypass original preview source CAS");area.untouched();
  }
}
}


namespace {
std::string namedMetadata(int kind){
  auto source=parse(metadata());
  check(source.replaceValue("attributes","d11:displayname14:retained-title6:future3:raw10:parametersd6:futurei7eee").has_value(),"Retained source title");
  if(kind!=0){
    const std::string info=kind==1?"d6:lengthi100e4:name6:single12:piece lengthi16384e6:pieces"+b(std::string(20,'s'))+'e'
        :"d5:filesld6:lengthi100e4:pathl9:first.bineee4:name6:bundle12:piece lengthi16384e6:pieces"+b(std::string(20,'o'))+'e';
    check(source.replaceValue("info",info).has_value(),"Exact single/one-entry-multi metadata");
  }
  return source.serialize();
}
auto namedImport(Area& area,const storage::TorrentIntakeMetadata& prepared,std::optional<std::string> name,
    void(*observer)(storage::TorrentIntakeStage)=nullptr){
  std::vector<compat::FilePolicy> policy;for(const auto& file:prepared.layout().files){policy.push_back({file.padding,0});}
  return storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,policy,observer,false,{}, {},false,false,false,{},std::move(name));
}
void saveNameOptions(){
  const std::string custom="chosen-\xd0\xbf\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0";
  for(int kind=0;kind<3;++kind){for(int choice=0;choice<3;++choice){
    Area area;const auto original=namedMetadata(kind);write(area.input,original);const auto prepared=take(storage::inspectTorrentIntake(area.input));
    const auto name=choice==0?std::optional<std::string>{}:std::optional<std::string>{choice==1?prepared.layout().name:custom};
    const auto hash=take(namedImport(area,prepared,name));const auto inventory=parse(read(area.profile/"downloads.config"));const auto& row=inventory.find("downloads")->children.at(0);
    check(row.find("save_file")->bytes==(choice==2?custom:prepared.layout().name)&&row.find("state")->integer==70&&!row.find("forceStart")->integer,"Only actual saved root/name changes");
    const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));const auto* attrs=state.find("attributes");
    check(attrs->find("displayname")->bytes==(choice==2&&kind!=2?custom:"retained-title"),"Exact original manual title rule incl one-entry-multi exception");
    const auto* links=attrs->find("filelinks2");check(links&&links->type==core::Node::Type::List&&links->children.empty(),"No synthesized links; original intake publishes explicit empty list");
    check(attrs->find("future")&&attrs->find("future")->bytes=="raw","Independent opaque source field remains intact");
    check(read(area.profile/"torrents"/(hash.hex()+".torrent"))==original&&read(area.input)==original,"Archive and input metadata are byte-identical");
    check(rawValue(state,"info")==rawValue(parse(original),"info")&&!fs::exists(area.destination),"Raw full identity and no payload allocation");
  }}
  for(const auto& name:std::vector<std::string>{"",".","..","../escape","part/leaf","part\\leaf",".git",".BiglyBTC.reserved","   ",std::string("bad\0name",8),"bad\nname",std::string("\xc0\xaf",2),std::string(256,'x')}){
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));check(!namedImport(area,prepared,name),"Unsafe/custom names refuse before effects");area.untouched();check(!fs::exists(area.root/"new"),"Refusal cannot create payload ancestors");
  }
  for(int kind=0;kind<3;++kind){
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto chosen=area.destination/"chosen";
    if(kind==0){write(chosen,"owned data");}else if(kind==1){fs::create_directories(chosen);}else{fs::create_directories(area.destination);fs::create_symlink(area.root/"absent",chosen);}
    check(!namedImport(area,prepared,std::string("chosen")),"Existing file/directory/symlink cannot be reused under a changed leaf");area.untouched();
    if(kind==0){check(read(chosen)=="owned data","No-clobber preserves existing bytes");}if(kind==2){check(fs::is_symlink(chosen),"No-clobber retains link identity");}
  }
  { Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));write(area.destination/"bundle/skip.bin","old unverified data");
    const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},false,false,false,{},std::string("chosen")));
    const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));check(state.find("attributes")->find("storetypes")->children[1].bytes=="C","Skipped-file absence uses effective leaf, not old metadata root");check(read(area.destination/"bundle/skip.bin")=="old unverified data"&&!fs::exists(area.destination/"chosen"),"Physical checks never touch old metadata-root bytes"); }

  for(const bool reserved:{false,true}){
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));area.destination=reserved?area.root/".git"/"payload":area.profile/"payload";
    check(!namedImport(area,prepared,std::string("chosen")),"Changed leaf cannot enter profile or reserved directory");area.untouched();check(!fs::exists(area.destination),"No unsafe ancestors created");
  }
  for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;write(area.input,namedMetadata(0));const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=stage;
    check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {7168,13312},true,false,true,core::TorrentIntakeTrackerTiers{},custom),"Interrupt accepted custom root with all other intents");
    area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered&&*recovered==prepared.hash(),"Fresh lease recovers exact custom-name identity");
    const auto inventory=parse(read(area.profile/"downloads.config"));const auto& row=inventory.find("downloads")->children.at(0);const auto state=parse(read(area.profile/"active"/(prepared.hash().hex()+".dat")));
    check(row.find("save_file")->bytes==custom&&row.find("maxdl")->integer==7168&&row.find("maxul")->integer==13312&&inventory.find("pause_data"),"Name/paused/rates are one recovered publication");
    check(state.find("attributes")->find("displayname")->bytes==custom&&core::originalDownloadFlags(state.find("attributes"))==std::optional<std::int64_t>{8192},"Title and flags survive every durable stage");check(!storage::hasPendingTorrentIntake(*area.lease)&&!fs::exists(area.destination),"Recovery retires journal without payload IO");
  }
  { Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
    check(!namedImport(area,prepared,std::string("chosen"),interrupt),"Accepted custom-root journal");write(area.destination/"chosen/first.bin","appeared after preview");const auto before=read(area.profile/"downloads.config");area.reopen();
    check(!storage::recoverTorrentIntake(*area.lease),"Fresh recovery rechecks complete no-clobber root even for selected files");check(read(area.destination/"chosen/first.bin")=="appeared after preview"&&read(area.profile/"downloads.config")==before&&storage::hasPendingTorrentIntake(*area.lease),"Failed recovery retains data, inventory and accepted intent"); }
  for(const auto& encoded:std::vector<std::string>{"0:",b("../wrong"),"i7e","le"}){
    Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;check(!namedImport(area,prepared,custom,interrupt),"Accepted name before malformed replay");
    auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));const auto* value=journal.find("fileOptions");check(value,"Real journal options");auto options=parse(core::encodeValue(*value,storage::compatibleConfigLimits()).value());check(options.replaceValue("saveName",encoded).has_value()&&journal.replaceValue("fileOptions",options.serialize()).has_value(),"Malformed save-name journal fixture");write(area.profile/"BiglyBTC.torrent-import",journal.serialize());area.reopen();
    check(!storage::recoverTorrentIntake(*area.lease)&&!fs::exists(area.profile/"active")&&!fs::exists(area.profile/"torrents")&&!fs::exists(area.destination),"Wrong/unsafe journal leaf cannot publish anything");
  }
  for(const bool external:{false,true}){
    Area area;const auto existing=take(storage::inspectTorrentIntake(area.input));take(namedImport(area,existing,std::nullopt));
    const auto chosen=area.destination/"chosen";auto inventory=parse(read(area.profile/"downloads.config"));auto old=parse(core::encodeValue(inventory.find("downloads")->children[0],storage::compatibleConfigLimits()).value());
    if(!external){check(old.replaceValue("save_file",b("chosen")).has_value()&&inventory.replaceValue("downloads","l"+old.serialize()+'e').has_value(),"Existing future root ownership");write(area.profile/"downloads.config",inventory.serialize());}
    else{auto state=parse(read(area.profile/"active"/(existing.hash().hex()+".dat")));auto attrs=parse(core::encodeValue(*state.find("attributes"),storage::compatibleConfigLimits()).value());const auto link="0\n"+(area.destination/"bundle/first.bin").string()+"\n"+(chosen/"linked.bin").string();check(attrs.replaceValue("filelinks2","l"+b(link)+'e').has_value()&&state.replaceValue("attributes",attrs.serialize()).has_value(),"Existing explicit external link owns new root");write(area.profile/"active"/(existing.hash().hex()+".dat"),state.serialize());}
    auto input=parse(metadata());auto info=parse(rawValue(input,"info"));check(info.replaceValue("name",b("other-source")).has_value()&&input.replaceValue("info",info.serialize()).has_value(),"Different actual torrent identity");write(area.input,input.serialize());const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto before=read(area.profile/"downloads.config");
    check(!namedImport(area,prepared,std::string("chosen"))&&read(area.profile/"downloads.config")==before&&!fs::exists(chosen),"Another saved/linked mapping claims absent chosen root; reject before mutation");
  }
}

} // namespace
namespace {
void uncertainSeedSave(storage::SaveStage stage){if(stage==storage::SaveStage::PrimaryRenamed){throw std::runtime_error("Actual primary already renamed");}}
void verifiedSeedIntentCases(){
  // Padding stays in original index order and policy, but owns no payload.
  for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;const auto source=metadata();const auto prepared=take(storage::inspectTorrentIntake(area.input));
    const std::array<compat::FilePolicy,4> selected{{{false,0},{false,0},{false,0},{true,0}}};
    interrupted=stage;check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,selected,interrupt,false,{}, {},false,false,false,{}, {},true),"Padded verified intake retains accepted interruption");
    area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered.has_value(),"Recover padded verified intent");
    const auto intent=take(storage::readVerifiedSeedIntent(*area.lease,*recovered));check(intent&&intent->state==storage::VerifiedSeedState::Pending&&intent->startup==core::StartupState::Stopped&&!intent->forceStart,"Padded recovery stays stopped and unverified");
    const auto state=parse(read(area.profile/"active"/(recovered->hex()+".dat")));check(rawValue(state,"info")==rawValue(parse(source),"info"),"Padded recovery preserves exact raw identity");
    check(state.find("attributes")->find("storetypes")->children[3].bytes=="C","Original pad remains absent compact/virtual, never selected L");
    check(read(area.profile/"torrents"/(recovered->hex()+".torrent"))==source&&!fs::exists(area.destination),"Padded recovery creates no pad or real payload");
  }
  {Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto before=read(area.profile/"downloads.config");
    const std::array<compat::FilePolicy,4> selectedPad{{{false,0},{false,0},{false,0},{false,0}}};
    check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,selectedPad,nullptr,false,{}, {},false,false,false,{}, {},true),"Selecting virtual padding remains refused before journal");
    check(read(area.profile/"downloads.config")==before&&!storage::hasPendingTorrentIntake(*area.lease)&&!fs::exists(area.destination),"Malformed pad authority has no effects");
  }
  const std::string source="d10:attributesd6:future3:raw5:flagsi-9223372036854775808ee4:infod6:lengthi1e4:name8:seed.bin12:piece lengthi16384e6:pieces20:hhhhhhhhhhhhhhhhhhhhee";
  const std::array<compat::FilePolicy,1> selected{{{false,0}}};
  const auto import=[&](Area& area,void(*observer)(storage::TorrentIntakeStage)=nullptr){
    write(area.input,source);const auto metadata=take(storage::inspectTorrentIntake(area.input));
    return storage::importTorrentWithFileOptions(*area.lease,metadata,area.destination,true,selected,observer,false,{}, {},false,false,false,{}, {},true);
  };
  for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area area;interrupted=stage;check(!import(area,interrupt),"Verified intent accepts actual durable interruption");area.reopen();
    const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered.has_value(),"Recover verified stopped intake");
    const auto intent=take(storage::readVerifiedSeedIntent(*area.lease,*recovered));check(intent&&intent->state==storage::VerifiedSeedState::Pending&&intent->startup==core::StartupState::Stopped&&!intent->forceStart&&!intent->paused,"Recovery remains stopped/unforced/unpaused, never assumed seeding");
    const auto state=parse(read(area.profile/"active"/(recovered->hex()+".dat")));check(core::originalDownloadFlags(state.find("attributes"))==std::optional<std::int64_t>{INT64_MIN|8},"Original move flag set before prepare, unknown signed bits preserved");
    check(read(area.profile/"torrents"/(recovered->hex()+".torrent"))==source&&!fs::exists(area.destination),"Archive raw bytes retained, no payload allocation");
    const auto prior=read(area.profile/"downloads.config");
    check(!storage::publishVerifiedSeed(*area.lease,*intent,uncertainSeedSave),"Failure after actual primary publication remains uncertain");
    const auto published=take(storage::readVerifiedSeedIntent(*area.lease,*recovered));check(published&&published->state==storage::VerifiedSeedState::Released&&published->startup==core::StartupState::Queued,"Actual primary contains queued75 but no invented successful acknowledgement");
    const auto after=read(area.profile/"downloads.config");check(after!=prior,"Actual row publication occurred");
    check(!storage::publishVerifiedSeed(*area.lease,*intent)&&read(area.profile/"downloads.config")==after,"Stale stopped intent cannot replay publication");
    check(take(storage::readVerifiedSeedIntent(*area.lease,*recovered))->row==published->row,"Opaque target and unrelated original data survive");
    check(storage::resetVerifiedSeedForRetry(*area.lease,*recovered).has_value(),"Explicit retry resets only saved intent");
    check(take(storage::readVerifiedSeedIntent(*area.lease,*recovered))->startup==core::StartupState::Stopped,"Retry never exposes queued before actual verification");
  }
  {Area area;write(area.input,source);const auto metadata=take(storage::inspectTorrentIntake(area.input));
    const auto ordinary=take(storage::importTorrentWithFileOptions(*area.lease,metadata,area.destination,true,selected));
    check(!take(storage::readVerifiedSeedIntent(*area.lease,ordinary)),"Old absent argument has no intent");
    auto doc=parse(read(area.profile/"downloads.config"));const auto row=rawValue(doc,"downloads");
    auto legacy=parse("d3:old"+row.substr(1,row.size()-2)+'e');write(area.profile/"downloads.config",legacy.serialize());
    check(!take(storage::readVerifiedSeedIntent(*area.lease,ordinary)),"Absent marker preserves legacy-map admission");
    write(area.profile/"downloads.config.bak",legacy.serialize());write(area.profile/"downloads.config","broken");
    check(!take(storage::readVerifiedSeedIntent(*area.lease,ordinary)),"Absent marker preserves Backup recovery policy");
  }
  {Area area;write(area.input,source);const auto metadata=take(storage::inspectTorrentIntake(area.input));
    const auto before=read(area.profile/"downloads.config");
    check(!storage::importTorrentWithFileOptions(*area.lease,metadata,area.destination,false,selected,nullptr,false,{}, {},false,true,false,{}, {},true),"Verified/queued conflict before journal");
    check(read(area.profile/"downloads.config")==before&&!storage::hasPendingTorrentIntake(*area.lease),"Conflicting request has no effects");
  }
}
}
int main(){
  try{
    verifiedSeedIntentCases();
    saveNameOptions();
    trackerOptions();
    for(const auto flags:std::array<std::int64_t,5>{0,1,8192,std::numeric_limits<std::int64_t>::min(),std::numeric_limits<std::int64_t>::max()}){
      for(const bool sequential:{false,true}){
        Area area;write(area.input,metadataWithFlags("i"+std::to_string(flags)+'e'));
        const auto prepared=take(storage::inspectTorrentIntake(area.input));
        const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},sequential));
        verify(area,hash,"LCLC",mixed);const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));
        const auto expected=flags|(sequential?core::OriginalSequentialDownloadFlag:0);
        check(core::originalDownloadFlags(state.find("attributes"))==std::optional<std::int64_t>{expected},"Sequential sets only its original bit; unknown signed bits survive");
      }
    }
    for(const auto* encoded:{"3:bad","le","de"}){
      Area area;write(area.input,metadataWithFlags(encoded));const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto refused=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{}, {},true);
      check(!refused&&refused.error().code==storage::ErrorCode::InvalidFormat,"Malformed saved flags refuse before commit");area.untouched();
    }
    for(const auto* encoded:{"i-1e","i2e","3:bad","le"}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {},true),"Freeze actual sequential intent");
      auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));auto bytes=core::encodeValue(*journal.find("fileOptions"),storage::compatibleConfigLimits());check(bool(bytes),"Accepted journal options");
      auto options=parse(std::move(*bytes));check(options.replaceValue("sequential",encoded).has_value(),"Own malformed sequential journal");
      check(journal.replaceValue("fileOptions",options.serialize()).has_value(),"Keep other journal authority");write(area.profile/"BiglyBTC.torrent-import",journal.serialize());
      const auto before=read(area.profile/"downloads.config");area.reopen();const auto recovered=storage::recoverTorrentIntake(*area.lease);
      check(!recovered&&recovered.error().code==storage::ErrorCode::InvalidFormat,"Malformed/nonboolean sequential replay refuses");
      check(read(area.profile/"downloads.config")==before&&!fs::exists(area.profile/"active")&&!fs::exists(area.destination)&&storage::hasPendingTorrentIntake(*area.lease),"Invalid intent retained without publication or payload");
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt),"Default old intake journal");
      const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(!journal.find("fileOptions")->find("sequential")&&!journal.find("fileOptions")->find("paused"),"Default API omits new optional journal fields");
      area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash.has_value(),"Old absent sequential field recovers");
      const auto state=parse(read(area.profile/"active"/(hash->hex()+".dat")));check(!state.find("attributes")->find("flags"),"Default old state does not materialize flags");
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));area.untouched();
      check(prepared.layout().files.size()==4&&prepared.layout().files[3].padding,"Actual validated metadata/padding");
      const auto result=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,false,mixed));
      verify(area,result,"LCLC",mixed);check(!fs::exists(area.root/"new"),"Intake never allocates payload");
      const auto state=parse(read(area.profile/"active"/(result.hex()+".dat")));
      const auto& flags=state.find("resume")->find("data")->find("resume data")->bytes;
      check(std::ranges::all_of(flags,[](char value){return value==2;}),"All actual pieces require recheck");
    }
    {
      Area area;write(area.profile/"biglybt.config","d27:Enable reorder storage modei1e27:Reorder storage mode min MBi0ee");
      fs::create_directories(area.destination/"bundle");write(area.destination/"bundle/last.bin",std::string(300,'z'));
      auto policy=mixed;policy[2]={true,0};
      const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,policy));
      verify(area,hash,"RXLX",policy); // Padding uses its source zero-size threshold too: X with min0.
      check(read(area.destination/"bundle/last.bin")==std::string(300,'z'),"Existing skipped payload never compacted");
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const std::vector<compat::FilePolicy> skipped(4,{true,0});
      const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,false,skipped,nullptr,true));
      verify(area,hash,"CCCC",skipped);
      const auto config=parse(read(area.profile/"downloads.config"));const auto& row=config.find("downloads")->children[0];
      check(row.find("state")->integer==70&&row.find("forceStart")->integer==0,"All skipped cannot auto-start/force");
    }
    for(const auto& limits:std::array<core::TorrentIntakeLimits,4>{{{65536,32768},{0,4096},{8192,0},{core::MaximumTorrentIntakeRate,core::MaximumTorrentIntakeRate}}}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{},limits));
      verify(area,hash,"LCLC",mixed);const auto config=parse(read(area.profile/"downloads.config"));const auto& row=config.find("downloads")->children.at(0);
      check(row.find("maxdl")->integer==limits.downloadBytesPerSecond&&row.find("maxul")->integer==limits.uploadBytesPerSecond,"Exact original per-torrent byte rates committed");
      check(!fs::exists(area.destination),"Rate choices cannot allocate payload");
    }
    for(const auto& limits:std::array<core::TorrentIntakeLimits,4>{{{-1,0},{0,-1},{core::MaximumTorrentIntakeRate+1,0},{0,core::MaximumTorrentIntakeRate+1}}}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto result=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,nullptr,false,{},limits);
      check(!result&&result.error().code==storage::ErrorCode::InvalidFormat,"Disabled/overflowed rate refuses before commit");area.untouched();
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt),"Freeze backward-compatible default rate journal");
      const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));const auto* options=journal.find("fileOptions");
      check(options&&!options->find("maxdl")&&!options->find("maxul"),"Unlimited defaults add no journal fields");
      area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered.has_value(),"Existing default journal remains recoverable");
      const auto saved=parse(read(area.profile/"downloads.config"));const auto& row=saved.find("downloads")->children.at(0);
      check(row.find("maxdl")->integer==0&&row.find("maxul")->integer==0,"Absent original rate fields default to unlimited");
    }
    for(const auto& value:{std::string("i-1e"),std::string("i2147483648e"),std::string("3:bad")}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {8192,16384}),"Freeze actual rate intent for recovery validation");
      auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));const auto* sourceOptions=journal.find("fileOptions");check(sourceOptions,"Actual rate options journal exists");
      auto encodedOptions=core::encodeValue(*sourceOptions,storage::compatibleConfigLimits());check(encodedOptions.has_value(),"Encode actual accepted rate intent");
      auto options=core::Document::parse(std::move(*encodedOptions),storage::compatibleConfigLimits());
      check(options&&options->replaceValue("maxdl",value).has_value(),"Own malformed rate field");
      check(journal.replaceValue("fileOptions",options->serialize()).has_value(),"Preserve all other journal authority");write(area.profile/"BiglyBTC.torrent-import",journal.serialize());
      const auto inventory=read(area.profile/"downloads.config");area.reopen();const auto recovered=storage::recoverTorrentIntake(*area.lease);
      check(!recovered&&recovered.error().code==storage::ErrorCode::InvalidFormat,"Malformed/negative/overflowed recovered rate refuses before publication");
      check(read(area.profile/"downloads.config")==inventory&&!fs::exists(area.profile/"active")&&!fs::exists(area.profile/"torrents")&&!fs::exists(area.destination),"Invalid recovered rates preserve inventory and payload");
      check(storage::hasPendingTorrentIntake(*area.lease),"Invalid intent retained for explicit recovery");
    }
    {
      Area direct,qualified;
      const auto previous="l"+b(std::string(20,'Q'))+"d4:hash"+b(std::string(20,'R'))+"5:forcei1e5:owneri44e6:future3:rawee";
      for(auto* area:{&direct,&qualified}){auto before=parse(read(area->profile/"downloads.config"));check(before.replaceValue("pause_data",previous).has_value(),"Existing legacy/unknown pause rows");write(area->profile/"downloads.config",before.serialize());}
      const auto prepared=take(storage::inspectTorrentIntake(direct.input));const auto reference=take(storage::inspectTorrentIntake(qualified.input));
      const auto hash=take(storage::importTorrentWithFileOptions(*direct.lease,prepared,direct.destination,true,mixed,nullptr,false,{}, {},false,false,true));
      const auto other=take(storage::importTorrentWithFileOptions(*qualified.lease,reference,qualified.destination,true,mixed));check(hash==other,"Same generated original identity");
      const auto command=take(storage::applyLibraryCommand(*qualified.lease,other,core::DownloadAction::Pause));check(command.pauseAfter&&command.pauseAfter->owner==1&&!command.pauseAfter->forceStart,"Qualified original general pause helper");
      const auto after=parse(read(direct.profile/"downloads.config"));const auto expected=parse(read(qualified.profile/"downloads.config"));
      check(pauseBytes(after)==pauseBytes(expected),"Pre-add pause encoding byte-exact to qualified Pause helper");
      check(pauseBytes(after).starts_with(previous.substr(0,previous.size()-1)),"Unknown and legacy pause rows retained verbatim");
      const auto library=core::readLibrary(after);check(library&&library->issues.empty()&&library->paused.size()==3,"Shared original pause reader admits new general pause");
      const auto& row=after.find("downloads")->children.at(0);check(row.find("state")->integer==70&&row.find("forceStart")->integer==0,"Paused saved row is unforced Stopped70");
      verify(direct,hash,"LCLC",mixed);check(!fs::exists(direct.destination),"Paused import has no payload effects");
    }
    for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
      for(const bool allSkipped:{false,true}){
        Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=stage;
        const auto policy=allSkipped?std::vector<compat::FilePolicy>(4,{true,0}):mixed;
        check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,policy,interrupt,false,{}, {},false,false,true),"Interrupt atomic pre-add pause");
        const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(bool(journal.find("fileOptions")->find("paused"))==!allSkipped,"All skipped removes pause intent before durable journal");
        area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash&&*hash==prepared.hash(),"Replay exact paused identity");
        const auto config=parse(read(area.profile/"downloads.config"));const auto saved=core::readLibrary(config);check(saved&&saved->issues.empty()&&saved->paused.size()==(allSkipped?0U:1U),"Pause survives every stage; all skipped remains ordinary stopped");
        check(!allSkipped||!config.find("pause_data"),"All skipped never fabricates an empty pause list");
        if(!allSkipped){check(saved->paused[0].hash==*hash&&!saved->paused[0].forceStart&&saved->paused[0].owner==1,"Exact original pause fields survive replay");}
        verify(area,*hash,allSkipped?"CCCC":"LCLC",policy);check(!fs::exists(area.destination),"Paused replay cannot allocate payload");
      }
    }
    for(int conflict=0;conflict<3;++conflict){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto refused=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,conflict!=0,mixed,nullptr,conflict==1,{}, {},false,conflict==2,true);
      check(!refused&&refused.error().code==storage::ErrorCode::InvalidFormat,"Paused rejects running/forced/queued conflicts before effects");area.untouched();
    }
    for(int mutation=0;mutation<6;++mutation){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {},false,false,true),"Freeze accepted paused authority");
      auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));
      if(mutation<2){check(journal.replaceValue(mutation==0?"stopped":"forceStart",mutation==0?"i0e":"i1e").has_value(),"Conflicting paused source flags");}
      else{auto value=core::encodeValue(*journal.find("fileOptions"),storage::compatibleConfigLimits());check(bool(value),"Accepted pause options");auto options=parse(std::move(*value));
        check(options.replaceValue(mutation==2?"queued":"paused",mutation==2?"i1e":mutation==3?"i2e":mutation==4?"i-1e":"3:bad").has_value(),"Invalid paused recovery field");check(journal.replaceValue("fileOptions",options.serialize()).has_value(),"Retain other accepted intent");}
      write(area.profile/"BiglyBTC.torrent-import",journal.serialize());const auto before=read(area.profile/"downloads.config");area.reopen();const auto refused=storage::recoverTorrentIntake(*area.lease);
      check(!refused&&refused.error().code==storage::ErrorCode::InvalidFormat,"Malformed/conflicting pause recovery refuses");check(read(area.profile/"downloads.config")==before&&!fs::exists(area.profile/"active")&&!fs::exists(area.destination)&&storage::hasPendingTorrentIntake(*area.lease),"Pause refusal preserves old inventory/no payload and retained intent");
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {},false,false,true),"Freeze paused full-inventory CAS");
      auto edited=parse(read(area.profile/"downloads.config"));check(edited.replaceValue("future","i88e").has_value(),"Independent original inventory edit");write(area.profile/"downloads.config",edited.serialize());const auto before=read(area.profile/"downloads.config");
      area.reopen();const auto refused=storage::recoverTorrentIntake(*area.lease);check(!refused&&refused.error().code==storage::ErrorCode::Conflict,"Changed whole inventory blocks atomic paused publication");check(read(area.profile/"downloads.config")==before&&!fs::exists(area.profile/"active"),"Conflicting CAS never adds a later pause write");
    }
    {
      Area area;interrupted=storage::TorrentIntakeStage::JournalSynced;check(!storage::importTorrent(*area.lease,area.input,area.destination,true,interrupt),"Freeze legacy default intake");
      const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(!journal.find("fileOptions"),"Legacy accepted journal has no new optional intent");
      area.reopen();const auto hash=take(storage::recoverTorrentIntake(*area.lease));check(hash.has_value(),"Legacy accepted journal recovers");const auto config=parse(read(area.profile/"downloads.config"));check(!config.find("pause_data"),"Legacy default does not materialize pause data");
    }
    for(const int mode:{0,1,2,3}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));const bool stopped=mode==0,force=mode==3,queued=mode==2;
      const auto hash=take(storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,stopped,mixed,nullptr,force,{}, {},false,queued));
      verify(area,hash,"LCLC",mixed);const auto config=parse(read(area.profile/"downloads.config"));const auto& row=config.find("downloads")->children.at(0);
      check(row.find("state")->integer==(stopped?70:queued?75:0)&&row.find("forceStart")->integer==(force?1:0),"Stopped70/legacy Waiting0/explicit Queued75/forced Waiting0 are distinct");
      check(!fs::exists(area.destination),"Start intent alone does not allocate payload");
    }
    for(const bool force:{false,true}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      const auto result=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,!force,mixed,nullptr,force,{}, {},false,true);
      check(!result&&result.error().code==storage::ErrorCode::InvalidFormat,"Queued cannot conflict with stopped or forced authority");area.untouched();
    }
    for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
      for(const int mode:{0,1,2,3}){
        Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=stage;
        const bool force=mode==1,queued=mode==0||mode==3,allSkipped=mode==3;
        const auto policy=allSkipped?std::vector<compat::FilePolicy>(4,{true,0}):mixed;
        check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,false,policy,interrupt,force,{}, {},false,queued),"Interrupt exact original start intent");
        area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));check(recovered&&*recovered==prepared.hash(),"Recover exact start identity");
        const auto config=parse(read(area.profile/"downloads.config"));const auto& row=config.find("downloads")->children.at(0);
        check(row.find("state")->integer==(allSkipped?70:queued?75:0)&&row.find("forceStart")->integer==(force?1:0),"Queued/forced/legacy/all-skipped intent survives every recovery stage");
        check(!fs::exists(area.destination),"Start intent replay cannot allocate payload");
      }
    }
    for(const int mutation:{0,1,2,3}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,false,mixed,interrupt,false,{}, {},false,true),"Freeze queued intent before any publication");
      auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));
      if(mutation<2){check(journal.replaceValue(mutation==0?"stopped":"forceStart","i1e").has_value(),"Conflicting start authority fixture");}
      else{const auto* node=journal.find("fileOptions");check(node,"Actual queued journal options");auto encoded=core::encodeValue(*node,storage::compatibleConfigLimits());check(encoded.has_value(),"Own original options");auto options=parse(*encoded);
        check(options.replaceValue("queued",mutation==2?"i2e":"3:bad").has_value()&&journal.replaceValue("fileOptions",options.serialize()).has_value(),"Malformed queued type/value fixture");}
      write(area.profile/"BiglyBTC.torrent-import",journal.serialize());const auto before=read(area.profile/"downloads.config");area.reopen();const auto recovery=storage::recoverTorrentIntake(*area.lease);
      check(!recovery&&recovery.error().code==storage::ErrorCode::InvalidFormat,"Recovery refuses malformed/conflicting queue authority");
      check(read(area.profile/"downloads.config")==before&&!fs::exists(area.profile/"active")&&!fs::exists(area.profile/"torrents")&&!fs::exists(area.destination)&&storage::hasPendingTorrentIntake(*area.lease),"Refusal preserves journal, metadata absence, inventory and payload");
    }
    for(int mode=0;mode<5;++mode){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));auto policy=mixed;
      if(mode==0){policy.pop_back();}if(mode==1){policy[3]={false,0};}if(mode==2){policy[0].priority=std::numeric_limits<std::int32_t>::min();}
      if(mode==4){policy[1].priority=1;}
      std::stop_source stop;if(mode==3){stop.request_stop();}
      const auto result=storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,policy,nullptr,false,stop.get_token());
      check(!result,"Invalid/cancelled choices reject");area.untouched();
    }
    {
      const std::array<compat::FilePolicy,8> values{{{false,0},{false,1},{false,2},{false,3},{false,4},{false,5},{false,6},{false,7}}};
      check(!compat::encodeOriginalFilePolicy(values),"Same shared seven-rank guard");
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));
      write(area.input,metadata()+"x");
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed),"Changed source CAS");
      area.untouched();
    }
    for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));const auto hash=prepared.hash();interrupted=stage;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt,false,{}, {7168,13312},true),"Interrupt actual durable stage");
      check(storage::hasPendingTorrentIntake(*area.lease),"Accepted options remain journalled");
      area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));
      check(recovered&&*recovered==hash,"Fresh held lease recovers exact options");
      verify(area,hash,"LCLC",mixed);check(!fs::exists(area.root/"new"),"Recovery never allocates payload");
      const auto saved=parse(read(area.profile/"downloads.config"));const auto& row=saved.find("downloads")->children.at(0);
      check(row.find("maxdl")->integer==7168&&row.find("maxul")->integer==13312,"Exact accepted rates survive every interrupted durable stage");
      const auto state=parse(read(area.profile/"active"/(hash.hex()+".dat")));check(core::originalDownloadFlags(state.find("attributes"))==std::optional<std::int64_t>{8192},"Exact sequential intent survives every interrupted durable stage");
    }
    {
      Area area;const auto prepared=take(storage::inspectTorrentIntake(area.input));interrupted=storage::TorrentIntakeStage::JournalSynced;
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt),"Freeze absent-skipped intent");
      write(area.destination/"bundle/skip.bin","foreign appeared");
      const auto before=read(area.profile/"downloads.config");
      check(!storage::recoverTorrentIntake(*area.lease),"Appeared leaf cannot become compact");
      check(read(area.destination/"bundle/skip.bin")=="foreign appeared"&&read(area.profile/"downloads.config")==before,"Conflict preserves data and inventory");
    }
    std::cout<<"PASS original pre-add options, absent/existing storage, selected progress, source/cancel guards, atomic replay and conflict\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
