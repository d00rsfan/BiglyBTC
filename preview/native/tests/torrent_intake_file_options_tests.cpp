#include "torrent_intake.hpp"
#include "library_commands.hpp"
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
