#include "torrent_intake.hpp"
#include "biglybtc/saved_dnd.hpp"
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
int main(){
  try{
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
      const auto journal=parse(read(area.profile/"BiglyBTC.torrent-import"));check(!journal.find("fileOptions")->find("sequential"),"Default API omits new optional journal field");
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
