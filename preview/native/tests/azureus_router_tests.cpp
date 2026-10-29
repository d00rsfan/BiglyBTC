#include "router.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace {
using namespace biglybtc::services::azureus_router;
std::size_t cases=0,assertions=0;
void check(bool value,const char* text){++assertions;if(!value){throw std::runtime_error(text);}}
template<class F>void run(const char* name,F body){try{body();++cases;}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}}
NodeId id(unsigned byte){NodeId value{};value[0]=static_cast<std::uint8_t>(byte);return value;}
ContactInput input(unsigned byte){ContactInput value;value.id=id(byte);return value;}
std::unique_ptr<RouterOwner> owner(RouterConfig policy={2,1,2,false}){
  auto value=RouterOwner::create(id(0),policy,17,10001);check(bool(value),"owner rejected explicit bounded config");return std::move(*value);
}
void accepted(const std::expected<RouterChange,RouterError>& value){check(bool(value),"valid monotone event rejected");}
RouterStatisticsSnapshot stats(RouterOwner& value){auto result=value.snapshot();check(bool(result),"owner snapshot unavailable");return *result;}
std::optional<RouterContact> find(RouterOwner& owner,unsigned byte){auto value=owner.find(id(byte));check(bool(value),"contact lookup failed");return *value;}
RouterContact required(RouterOwner& owner,unsigned byte){auto value=find(owner,byte);check(value.has_value(),"expected stored contact missing");return *value;}
std::unique_ptr<RouterOwner> far(RouterConfig config={2,1,2,false}){
  auto value=owner(config);accepted(value->contactKnown(input(0x80),10002));accepted(value->contactKnown(input(0x40),10003));
  accepted(value->contactKnown(input(0x20),10004));accepted(value->contactAlive(input(0xc0),10005));return value;
}
}
static_assert(!std::is_default_constructible_v<RouterStatisticsSnapshot>);
int main(int argc,char** argv){try{
  check(argc==2,"explicit source-cases.tsv path required");
  run("literal source policy sequence with seven independently frozen counters",[&]{
    std::ifstream file(argv[1]);check(file.good(),"cannot read explicit fixture");auto value=owner();std::string line;std::size_t rows=0;
    while(std::getline(file,line)){if(line.empty()||line[0]=='#'){continue;}check(line.size()<512,"unbounded fixture line");std::istringstream row(line);
      char operation=0;std::string hex;std::int64_t now=0;int force=0;std::array<std::uint64_t,7> expected{};
      check(bool(row>>operation>>hex>>now>>force),"bad fixture fields");for(auto& n:expected){check(bool(row>>n),"missing frozen counter");}
      std::string extra;check(!(row>>extra),"extra fixture column");check(hex.size()==2,"bad fixture node byte");const auto byte=static_cast<unsigned>(std::stoul(hex,nullptr,16));
      if(operation=='K'){accepted(value->contactKnown(input(byte),now,force!=0));}else if(operation=='A'){accepted(value->contactAlive(input(byte),now));}
      else if(operation=='D'){accepted(value->contactDead(id(byte),now,force!=0));}else{check(operation=='N'&&rows==0,"unknown fixture operation");}
      check(stats(*value).counts()==expected,"router differs from literal source-defined counter sequence");++rows;
    }
    check(rows==20,"fixture row count changed");check(required(*value,0xe0).bucket&&required(*value,0xf0).bucket,"newest live replacement promotion lost");
    check(find(*value,0x80)&&!required(*value,0x80).bucket&&!required(*value,0x80).hasBeenAlive,"force-known did not reintroduce an unknown replacement");
  });
  run("local node always survives and snapshots are owned",[]{
    auto value=owner();const auto old=stats(*value);accepted(value->contactDead(id(0),10002,true));auto sleeping=input(0);sleeping.sleeping=true;
    accepted(value->contactAlive(sleeping,10003));check(required(*value,0).alive(),"local algorithmic contact became dead");
    accepted(value->contactAlive(input(0x80),10004));check(old.counts()==std::array<std::uint64_t,7>{1,1,1,0,1,0,0},"old sealed snapshot aliased new owner state");
    auto copy=required(*value,0x80);copy.failures=99;check(copy.failures==99&&required(*value,0x80).alive(),"contact copy mutated owner");
  });
  run("live and unknown failure thresholds belong to the attachment",[]{
    auto value=owner();auto node=input(0x80);node.maxFailUnknown=2;node.maxFailLive=3;
    accepted(value->contactKnown(node,10002));accepted(value->contactDead(node.id,10003));check(required(*value,0x80).failing(),"unknown first failure ignored");
    accepted(value->contactDead(node.id,10004));check(!find(*value,0x80),"unknown threshold failed to remove");
    accepted(value->contactAlive(node,10005));accepted(value->contactDead(node.id,10006));accepted(value->contactDead(node.id,10007));
    check(find(*value,0x80)&&required(*value,0x80).failures==2,"live threshold used unknown limit");accepted(value->contactDead(node.id,10008));check(!find(*value,0x80),"live threshold failed to remove");
  });
  run("replacement preference evicts unknown and promotes most recent ever-live",[]{
    auto value=far();accepted(value->contactKnown(input(0xe0),10006));accepted(value->contactAlive(input(0xf0),10007));
    accepted(value->contactKnown(input(0xa0),10008));check(!find(*value,0xe0)&&find(*value,0xf0),"new unknown displaced a live replacement");
    accepted(value->contactAlive(input(0xe0),10009));check(!find(*value,0xa0),"live replacement failed to evict unknown");
    const auto dropped=value->contactKnown(input(0xa0),10010,true);check(dropped&&*dropped==RouterChange::Dropped,"unknown replaced all-live list");
    accepted(value->contactAlive(input(0xd0),10011));check(!find(*value,0xf0),"new live did not replace oldest live replacement");
    accepted(value->contactDead(id(0x80),10012,true));check(required(*value,0xd0).bucket&&!required(*value,0xe0).bucket,"promotion did not choose newest ever-live replacement");
  });
  run("alive bucket LRU controls inert oldest ping intent",[]{
    auto value=far();check(bool(value->takeIntents()),"cannot drain setup intents");accepted(value->contactAlive(input(0x80),10006));
    accepted(value->contactKnown(input(0xe0),10007));const auto intents=value->takeIntents();check(bool(intents)&&intents->count==1,"replacement emitted unexpected controller actions");
    check(intents->values[0].kind==RouterIntentKind::Ping&&intents->values[0].id==id(0xc0),"oldest nonlocal bucket member was not selected");
    check(required(*value,0xc0).pingOutstanding,"intent did not mark outstanding probe");accepted(value->contactAlive(input(0xc0),10008));check(!required(*value,0xc0).pingOutstanding,"actual alive feedback did not clear ping marker");
  });
  run("instance changes throttle node-add intents and zero is indirect identity",[]{
    auto value=owner();auto node=input(0x80);node.instance=1;accepted(value->contactAlive(node,10002));check(value->takeIntents()->count==1,"new bucket contact missing inert node-add");
    node.instance=2;accepted(value->contactKnown(node,11000,true));check(value->takeIntents()->count==0,"instance flip bypassed 10-second add throttle");check(required(*value,0x80).instance==2,"new nonzero instance ignored");
    node.instance=3;accepted(value->contactKnown(node,21003,true));check(value->takeIntents()->count==1,"later instance change not reported");
    node.instance=0;accepted(value->contactKnown(node,21004,true));check(required(*value,0x80).instance==3,"indirect instance0 replaced known identity");
  });
  run("known dedup force and strict ten-minute reset",[]{
    auto value=owner();accepted(value->contactKnown(input(0x80),10002));accepted(value->contactDead(id(0x80),10003,true));
    auto result=value->contactKnown(input(0x80),10004);check(result&&*result==RouterChange::Ignored&&!find(*value,0x80),"recent-known gate did not preserve removed contact");
    accepted(value->contactKnown(input(0x80),610002));check(find(*value,0x80).has_value(),"known window did not expire strictly after ten minutes");
    check(stats(*value).suppressedKnown()==1,"known suppression observation missing");
  });
  run("sleep suspend and bootstrap policy never invent network activity",[]{
    auto value=far();check(bool(value->takeIntents()),"setup drain failed");check(bool(value->setSuspended(true,10006)),"suspend failed");
    accepted(value->contactDead(id(0x80),10007,true));check(find(*value,0x80).has_value(),"suspended failure removed contact");
    accepted(value->contactKnown(input(0xe0),10008));check(value->takeIntents()->count==0,"suspended owner emitted network intent");
    check(bool(value->setSuspended(false,10009))&&bool(value->setSleeping(true,10010)),"policy toggle failed");
    accepted(value->contactAlive(input(0xf0),10011));check(stats(*value).counts()[3]==1,"sleeping replacement limit not applied");
    auto proxy=far({2,1,2,true});accepted(proxy->contactDead(id(0x80),10006,true));check(find(*proxy,0x80).has_value(),"bootstrap proxy removed without available replacement");
    accepted(proxy->contactKnown(input(0xe0),10007));accepted(proxy->contactDead(id(0x80),10008,true));check(!find(*proxy,0x80)&&required(*proxy,0xe0).bucket,"proxy failed to rotate with a replacement");
  });
  run("continuous network failures stop eviction at one hundred unless forced",[]{
    auto value=owner();accepted(value->contactAlive(input(0x80),10002));for(int i=0;i<99;++i){accepted(value->contactDead(id(0x40),10003+i));}
    accepted(value->contactDead(id(0x80),10102));check(required(*value,0x80).alive(),"network outage protection ignored");
    accepted(value->contactAlive(input(0x80),10103));check(stats(*value).consecutiveDead()==0,"alive feedback did not reset outage counter");
    accepted(value->contactDead(id(0x80),10104,true));check(!find(*value,0x80),"force did not bypass failure threshold");
  });
  run("native quotas are explicit actual bounded counts not estimated capacity",[]{
    auto value=owner({2,16,2,false});for(unsigned i=1;i<=4096;++i){ContactInput node;node.id[0]=static_cast<std::uint8_t>(i>>8);node.id[1]=static_cast<std::uint8_t>(i);accepted(value->contactAlive(node,10001+i));}
    const auto snapshot=stats(*value);check(snapshot.capacityDrops()>0&&snapshot.limited(),"native resource exhaustion not visible");check(snapshot.lostIntents()>0,"bounded unconsumed intent loss hidden");
    const auto& n=snapshot.counts();check(n[0]<=MaximumRouterNodes&&n[1]<=MaximumRouterBuckets&&n[2]+n[3]<=MaximumRouterContacts,"hard quota exceeded");
    check(n[0]==2*n[1]-1&&n[2]==n[4]+n[5]+n[6],"actual tree/contact partition inconsistent");
    std::uint64_t contacts=0,replacements=0;for(const auto& bucket:snapshot.buckets()){contacts+=bucket.contacts;replacements+=bucket.replacements;check(bucket.depth<=160&&bucket.contacts<=2,"invalid bucket geometry");}
    check(contacts==n[2]&&replacements==n[3],"sealed bucket facts differ from totals");
  });
  run("thread clock and invalid attachment errors do not mutate owner",[]{
    auto value=owner();const auto before=stats(*value);auto bad=input(0x80);bad.maxFailLive=0;check(!value->contactAlive(bad,10002),"invalid failure threshold admitted");
    check(!value->contactAlive(input(0x80),10000),"clock rollback admitted");check(stats(*value).revision()==before.revision(),"rejected call mutated revision");
    bool denied=false;std::thread worker([&]{const auto result=value->snapshot();denied=!result&&result.error()==RouterError::Thread;});worker.join();check(denied,"foreign snapshot escaped owner fence");
    check(!RouterOwner::create(id(0),{0,1,2,false}),"invalid K admitted");check(!RouterOwner::create(id(0),{2,0,2,false}),"invalid B admitted");
  });

  run("closest traversal returns full best bucket and never replacement authority",[]{
    auto value=far();accepted(value->contactKnown(input(0xe0),10006));
    const auto best=value->closest(id(0x80),1,true);check(best&&best->count==2,"closest truncated source whole bucket");
    check(best->values[0].id==id(0x80)&&best->values[1].id==id(0xc0),"best-bit bucket order changed");
    const auto outside=value->localInClosest(id(0xc0),2,true);check(outside&&!*outside,"local outside returned closest bucket gained authority");
    const auto inside=value->localInClosest(id(0xc0),4,true);check(inside&&*inside,"source local membership/distance predicate lost");
    accepted(value->contactDead(id(0x80),10007));const auto live=value->closest(id(0x80),1,true);check(live&&live->count==1&&live->values[0].id==id(0xc0),"failing bucket member not filtered");
    const auto all=value->closest(id(0x80),1,false);check(all&&all->count==2,"non-live source traversal lost failing contact");
    check(!value->closest(id(0),0)&&!value->localInClosest(id(0),MaximumRouterContacts+1),"unbounded closest query admitted");
  });
  run("live-only closest includes unknown bucket members and requires local membership",[]{
    auto value=owner({4,1,2,false});accepted(value->contactKnown(input(0x80),10002));
    const auto result=value->closest(id(0x80),1,true);check(result&&result->count==2,"live-only incorrectly required previously alive");
    check(!result->values[1].hasBeenAlive,"unknown setup was promoted by query");
    const auto closest=value->localInClosest(id(0x80),1,true);check(closest&&!*closest,"closer unknown did not count against local distance");
    const auto before=stats(*value).revision();check(bool(value->closest(id(0),2,true)),"valid closest query failed");check(stats(*value).revision()==before,"read-only closest mutated router");
  });
  std::cout<<"PASS "<<cases<<" router policy families; "<<assertions<<" assertions; source-derived fixtures, no Java runtime equality credit\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL router: "<<e.what()<<'\n';return 1;}}
