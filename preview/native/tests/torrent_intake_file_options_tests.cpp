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
      check(!storage::importTorrentWithFileOptions(*area.lease,prepared,area.destination,true,mixed,interrupt),"Interrupt actual durable stage");
      check(storage::hasPendingTorrentIntake(*area.lease),"Accepted options remain journalled");
      area.reopen();const auto recovered=take(storage::recoverTorrentIntake(*area.lease));
      check(recovered&&*recovered==hash,"Fresh held lease recovers exact options");
      verify(area,hash,"LCLC",mixed);check(!fs::exists(area.root/"new"),"Recovery never allocates payload");
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
