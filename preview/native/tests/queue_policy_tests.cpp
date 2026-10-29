#include "biglybtc/queue_policy.hpp"
#include "biglybtc/queue_observations.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace biglybtc::core;
std::size_t assertions=0,families=0;
void check(bool value,const char* text){++assertions;if(!value){throw std::runtime_error(text);}}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)),#__VA_ARGS__)
template<class F>void run(const char* name,F&& f){try{f();++families;}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}}
std::string bytes(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
Document config(std::initializer_list<std::pair<std::string,std::string>> fields){
  std::string data="d";for(const auto& [key,value]:fields){data+=bytes(key)+value;}data+='e';
  auto document=Document::parse(data);CHECK(document);return std::move(*document);
}
QueuePolicy read(const Document* d=nullptr){auto p=readQueuePolicy(d);CHECK(p);return *p;}
QueueContext context(std::int64_t now=100000){QueueContext c;c.nowMillis=now;c.smoothedUploadBytesPerSecond=0;return c;}
QueueTorrentFacts download(std::uint64_t token=1,std::uint32_t position=1){
  QueueTorrentFacts f;f.token=token;f.position=position;f.shareRatio=1000;
  f.secondsDownloading=100;f.secondsSeeding=0;f.smoothedUploadBytesPerSecond=0;
  f.downloadBytesPerSecond=0;f.uploadBytesPerSecond=0;f.diskReadLatencyMillis=0;f.diskWriteLatencyMillis=0;
  return f;
}
QueueTorrentFacts seed(std::uint64_t token=1,std::uint32_t position=1){
  auto f=download(token,position);f.complete=true;f.secondsSeeding=1000;f.swarm=QueueSwarm{1,4,true};return f;
}
QueuePlan plan(const QueuePolicy& p,std::span<const QueueTorrentFacts> f,const QueueContext& c=context()){
  auto r=evaluateQueue(p,f,c);CHECK(r);return std::move(*r);
}
QueueDecision decide(const QueuePolicy& p,const QueueTorrentFacts& f,const QueueContext& c=context()){
  return plan(p,std::span(&f,1),c).decisions.front();
}
} // namespace
int main(){try{
  run("actual announce defers regular due without inventing response or clearing pending",[]{
    QueueOriginalTrackerResponse response;const auto initial=response.response();response.deferRegularUntil(1000);CHECK(response.response().type==initial.type);CHECK(!response.regularPollDue(999)&&response.regularPollDue(1000));
    response.deferRegularUntil(500);CHECK(!response.regularPollDue(999));response.requested();response.deferRegularUntil(2000);CHECK(!response.regularPollDue(3000));response.cancelRequest();CHECK(!response.regularPollDue(1999)&&response.regularPollDue(2000));
    QueueTrackerTerminal good;good.kind=QueueTrackerTerminalKind::Announce;good.comparable=true;good.present=3;good.complete=5;good.incomplete=6;good.observedMillis=10;good.observedWallMillis=20;response.observe(good);response.deferRegularUntil(4000);CHECK(response.response().content->seeds==5);CHECK(!response.regularPollDue(3999));
    QueueTrackerTerminal error;error.kind=QueueTrackerTerminalKind::ScrapeFailed;error.definitiveFailure=true;error.observedMillis=5000;response.observe(error);response.deferRegularUntil(100);CHECK(!response.regularPollDue(604999)&&response.regularPollDue(605000));CHECK(response.response().content->seeds==5);
  });
  run("original defaults and modern versus az2 first priority",[]{
    const auto p=read();CHECK(p.maximumActive==4&&p.maximumDownloads==4&&p.minimumDownloads==1);
    CHECK(p.rankMode==QueueRankMode::SeedPeerRatio&&p.firstPriorityMode==QueueFirstPriorityMode::Any);
    CHECK(p.firstPriorityShareRatio==500&&p.ignoreShareRatio==0&&p.ignoreZeroPeers&&p.firstPriorityIgnoreZeroPeers);
    CHECK(p.minimumSeedSeconds==300&&p.minimumSeedActiveSeconds==900&&p.maximumStalledSeeds==5);
    CHECK(p.minimumDownloadBytesPerSecond==512&&p.minimumSeedBytesPerSecond==512);
    auto d=config({{"ui",bytes("az2")}});CHECK(!read(&d).firstPriorityIgnoreZeroPeers);
    CHECK(read().reservedLightSeedSlots==4&&read().flexibleSeedSlots==5);
  });
  run("strict config errors carry exact key without clamping or materializing",[]{
    for(const auto& value:std::array<std::string,3>{"i-1e",bytes("wrong"),"li4ee"}){
      auto d=config({{"max downloads",value}});const auto p=readQueuePolicy(&d);
      CHECK(!p&&p.error().key=="max downloads");
    }
    // The owning parser rejects duplicate dictionaries before a Document can
    // enter the policy API. Do not require this deliberately invalid fixture
    // to pass the valid-document helper.
    const auto duplicate=Document::parse("d"+bytes("max downloads")+"i1e"+bytes("max downloads")+"i2ee");
    CHECK(!duplicate&&duplicate.error().code=="duplicate_key");
    auto boolean=config({{"StartStopManager_bIgnore0Peers","i2e"}});CHECK(!readQueuePolicy(&boolean));
    auto duration=config({{"StartStopManager_iMinSeedingTime","i2147484e"}});
    CHECK(readQueuePolicy(&duration).error().key=="StartStopManager_iMinSeedingTime");
    auto none=Document::parse("le");CHECK(none&&!readQueuePolicy(&*none));
  });
  run("original Float byte string and numeric conversion uses thousandths",[]{
    auto d=config({{"Stop Ratio",bytes("1.999")}});CHECK(read(&d).ignoreShareRatio==1999);
    auto integer=config({{"Stop Ratio","i2e"}});CHECK(read(&integer).ignoreShareRatio==2000);
    auto plus=config({{"Stop Ratio",bytes(" +0.5 ")}});CHECK(read(&plus).ignoreShareRatio==500);
    for(const auto text:{"NaN","Infinity","-1","bad","2147484"}){
      auto bad=config({{"Stop Ratio",bytes(text)}});CHECK(!readQueuePolicy(&bad));
    }
    CHECK(d.originalBytes()==d.serialize());
  });
  run("literal ordinary seed rank formulas and fallback boundary",[]{
    auto p=read();auto f=seed();CHECK(decide(p,f).seedingRank==63999); //99999/(1.25 squared)
    f.swarm=QueueSwarm{0,1,true};CHECK(decide(p,f).seedingRank==99999);
    p.minimumPeersToBoostNoSeeds=2;CHECK(decide(p,f).seedingRank==0);
    p.rankMode=QueueRankMode::SeedCount;f.swarm=QueueSwarm{3,9,true};CHECK(decide(p,f).seedingRank==999700000);
    p.seedCountFallback=3;CHECK(decide(p,f).seedingRank==56249);
    p.rankMode=QueueRankMode::PeerCount;CHECK(decide(p,f).seedingRank==2025);
    f.swarm=QueueSwarm{0,11,true};CHECK(decide(p,f).seedingRank==11000);
    p.rankMode=QueueRankMode::None;CHECK(decide(p,f).seedingRank==0);
  });
  run("Java wrapping rank extremes do not overflow native signed math",[]{
    auto p=read();p.rankMode=QueueRankMode::PeerCount;auto f=seed();
    f.swarm=QueueSwarm{0,std::numeric_limits<std::int32_t>::max(),true};CHECK(decide(p,f).seedingRank==1);
    f.swarm=QueueSwarm{std::numeric_limits<std::int32_t>::max(),0,true};CHECK(decide(p,f).seedingRank==-7);
    p.ignoreZeroPeers=false;CHECK(decide(p,f).seedingRank==0);
    f=download();f.position=2147483647;CHECK(decide(p,f).seedingRank==-1147473647);
  });
  run("first priority any all strict ratio and enabled duration criteria",[]{
    auto p=read();auto f=seed();f.shareRatio=499;CHECK(decide(p,f).firstPriority);
    f.shareRatio=500;CHECK(!decide(p,f).firstPriority);f.shareRatio=-1;CHECK(!decide(p,f).firstPriority);
    p.firstPrioritySeedMinutes=90;f.secondsSeeding=5399;CHECK(decide(p,f).firstPriority);
    f.secondsSeeding=5400;CHECK(!decide(p,f).firstPriority);
    p.firstPriorityMode=QueueFirstPriorityMode::All;f.shareRatio=100;CHECK(!decide(p,f).firstPriority);
    f.secondsSeeding=5399;CHECK(decide(p,f).firstPriority);
    f.minimumShareRatio=50;CHECK(!decide(p,f).firstPriority);
    f.externallyFirstPriority=true;f.externallyNotFirstPriority=true;CHECK(decide(p,f).firstPriority);
    f.externallyFirstPriority=false;CHECK(!decide(p,f).firstPriority);
  });
  run("first priority valid zero peers integer SP and strict idle boundaries",[]{
    auto p=read();auto f=seed();f.shareRatio=0;f.swarm=QueueSwarm{8,0,true};CHECK(!decide(p,f).firstPriority);
    f.swarm->valid=false;CHECK(decide(p,f).firstPriority);f.swarm.reset();CHECK(decide(p,f).firstPriority);
    p.firstPriorityIgnoreSeedPeerRatio=10;f.swarm=QueueSwarm{19,2,true};CHECK(decide(p,f).firstPriority);
    f.swarm->seeds=20;CHECK(!decide(p,f).firstPriority);
    f.swarm=QueueSwarm{1,4,true};p.firstPriorityIgnoreIdleMinutes=1;f.secondsSinceLastUpload=60;CHECK(decide(p,f).firstPriority);
    f.secondsSinceLastUpload=61;CHECK(!decide(p,f).firstPriority);
    f.secondsSinceLastUpload.reset();f.secondsSeeding=61;CHECK(!decide(p,f).firstPriority);
  });
  run("ignore precedence overrides infinity and first priority bypass",[]{
    auto p=read();auto f=seed();p.ignoreShareRatio=1000;p.ignoreSeedCount=2;p.ignorePeerSeedRatio=2;
    f.shareRatio=1000;f.swarm=QueueSwarm{3,0,true};CHECK(decide(p,f).seedingRank==-8);
    f.shareRatio=-1;CHECK(decide(p,f).seedingRank==-7);
    f.swarm->peers=4;CHECK(decide(p,f).seedingRank==-5);
    p.ignoreSeedCount=0;CHECK(decide(p,f).seedingRank==-4);
    f.shareRatio=499;CHECK(decide(p,f).firstPriority&&decide(p,f).seedingRank>=0);
    f.maximumShareRatio=250;CHECK(decide(p,f).firstPriority); //FP still overrides torrent max.
  });
  run("missing swarm is never fabricated zero and exact no-scrape gate persists",[]{
    auto p=read();auto f=seed();f.swarm.reset();const auto d=decide(p,f,context(1000000));
    CHECK(d.seedingRank==-9&&d.action==QueueAction::Queue&&d.reason==QueueReason::UnknownFacts);
    const auto result=plan(p,std::span(&f,1));CHECK(result.unavailableInputs&1U);
    f.swarm=QueueSwarm{0,0,true};CHECK(decide(p,f).seedingRank==-7);
    f.swarm=QueueSwarm{0,0,false};CHECK(decide(p,f).seedingRank==-9);
  });
  run("minimum seed time preserves actual running work before ignore stop",[]{
    auto p=read();auto f=seed();f.state=QueueTorrentState::Seeding;f.swarm=QueueSwarm{1,0,true};f.runningMillis=299999;
    CHECK(decide(p,f).action==QueueAction::Preserve&&decide(p,f).reason==QueueReason::MinimumSeedingTime);
    f.runningMillis=300000;CHECK(decide(p,f).action==QueueAction::Queue);
    f.smoothedUploadBytesPerSecond=100;auto c=context();c.smoothedUploadBytesPerSecond=100;
    f.runningMillis=899999;CHECK(decide(p,f,c).reason==QueueReason::MinimumSeedingTime);
    f.runningMillis=900000;CHECK(decide(p,f,c).action==QueueAction::Queue);
    f.smoothedUploadBytesPerSecond.reset();f.runningMillis=300000;CHECK(decide(p,f,c).reason==QueueReason::MinimumSeedingTime);
  });
  run("manual pause stopped failed and preparing are never resumed",[]{
    auto p=read();auto f=download();f.forceStart=true;f.manuallyPaused=true;
    CHECK(decide(p,f).action==QueueAction::Preserve&&decide(p,f).reason==QueueReason::ManualPause);
    f.manuallyPaused=false;f.state=QueueTorrentState::Stopped;CHECK(decide(p,f).action==QueueAction::Preserve);
    f.state=QueueTorrentState::Failed;CHECK(decide(p,f).action==QueueAction::Preserve);
    f.forceStart=false;f.state=QueueTorrentState::Preparing;CHECK(decide(p,f).reason==QueueReason::Preparing);
    f.state=QueueTorrentState::Checking;CHECK(decide(p,f).reason==QueueReason::Checking);
  });
  run("force download bypasses slots while force seed consumes a slot",[]{
    auto p=read();p.maximumActive=1;p.maximumDownloads=1;p.minimumDownloads=0;
    auto a=download(1,1),b=download(2,2);a.forceStart=true;
    auto r=plan(p,std::array{a,b});CHECK(r.decisions[0].reason==QueueReason::Forced&&r.decisions[1].action==QueueAction::Run);
    auto s=seed(3,3);s.forceStart=true;s.state=QueueTorrentState::Seeding;s.runningMillis=0;
    r=plan(p,std::array{s,b});CHECK(r.decisions[1].action==QueueAction::Queue);
    p.minimumDownloads=1;r=plan(p,std::array{s,b});CHECK(r.decisions[1].action==QueueAction::Run);
    s.newlyCompleted=true;const auto transition=decide(p,s);CHECK(transition.clearForceStart&&transition.moveNewSeedToTop);
    p.retainForceStartOnComplete=true;CHECK(!decide(p,s).clearForceStart);
  });
  run("queue caps order stable tokens zero unlimited and linked minimum",[]{
    auto p=read();p.maximumDownloads=1;p.maximumActive=1;p.minimumDownloads=0;
    const auto a=download(9,3),b=download(7,1),c=download(8,2);
    auto r=plan(p,std::array{a,b,c});CHECK(r.decisions[0].token==9&&r.decisions[0].action==QueueAction::Queue);
    CHECK(r.decisions[1].action==QueueAction::Run&&r.decisions[2].action==QueueAction::Queue);
    p.maximumDownloads=0;r=plan(p,std::array{a,b,c});for(const auto& d:r.decisions){CHECK(d.action==QueueAction::Run);}
    p.maximumDownloads=2;p.linkMinimumDownloads=true;r=plan(p,std::array{a,b,c});CHECK(r.decisions[2].action==QueueAction::Run);
  });
  run("mapped source stale timing uses measured latency and loses authority across gaps",[]{
    auto p=read();auto f=seed();f.sourceStaleTiming=true;f.state=QueueTorrentState::Seeding;
    f.runningMillis=100000;f.activity.seeding=true;
    auto d=decide(p,f,context(60000));CHECK(d.activelySeeding&&d.staleRankOffset==0);f.activity=d.activity;
    d=decide(p,f,context(70000));CHECK(!d.activelySeeding&&d.staleRankOffset==1&&d.seedingRank==63998);f.activity=d.activity;
    d=decide(p,f,context(130000));CHECK(d.staleRankOffset==1&&d.seedingRank==63998);f.activity=d.activity;
    d=decide(p,f,context(130001));CHECK(d.staleRankOffset==2&&d.seedingRank==63997);f.activity=d.activity;
    f.diskReadLatencyMillis.reset();auto r=plan(p,std::span(&f,1),context(190002));d=r.decisions.front();
    CHECK(!d.staleRankOffset&&(r.unavailableInputs&8U)&&(r.unavailableInputs&64U));
    CHECK(d.seedingRank==63999);f.activity=d.activity;
    f.diskReadLatencyMillis=0;d=decide(p,f,context(190003));CHECK(!d.staleRankOffset);f.activity=d.activity;
    f.uploadBytesPerSecond=512;d=decide(p,f,context(190004));CHECK(!d.staleRankOffset);f.activity=d.activity;
    d=decide(p,f,context(200004));CHECK(d.activelySeeding&&d.staleRankOffset==0);f.activity=d.activity;
    f.uploadBytesPerSecond=0;d=decide(p,f,context(200005));f.activity=d.activity;
    d=decide(p,f,context(210005));CHECK(!d.activelySeeding&&d.staleRankOffset==1);
    // A genuinely new admission has no inherited activity/stale clock.
    f.activity={};f.token=2;d=decide(p,f,context(210006));CHECK(d.staleRankOffset==0);
  });
  run("source stale rank feedback and fast-rate latency branch are exact",[]{
    auto p=read();auto f=seed();f.sourceStaleTiming=true;f.state=QueueTorrentState::Seeding;
    f.runningMillis=100000;f.activity.seeding=true;f.activity.seedChangeMillis=0;
    // Explicit controlled monotone facts exercise the original refresh epoch0
    // and rank<=offset reset, without waiting days or inventing executor delay.
    auto d=decide(p,f,context(4000000000LL));CHECK(d.seedingRank==63999&&d.staleRankOffset==0);
    auto down=download();down.state=QueueTorrentState::Downloading;down.runningMillis=100000;
    down.activity.downloading=true;down.downloadBytesPerSecond=512;down.diskWriteLatencyMillis=9000;
    d=decide(p,down,context(100000));CHECK(d.activelyDownloading&&!d.activity.downloadHighLatencyMillis);down.activity=d.activity;
    down.downloadBytesPerSecond=0;down.diskWriteLatencyMillis=0;
    d=decide(p,down,context(100001));down.activity=d.activity;
    d=decide(p,down,context(110001));CHECK(!d.activelyDownloading); // No invented60s recovery from the fast tick.
    auto up=seed();up.state=QueueTorrentState::Seeding;up.runningMillis=100000;
    up.activity.seeding=true;up.uploadBytesPerSecond=512;up.diskReadLatencyMillis=9000;
    d=decide(p,up,context(100000));CHECK(d.activity.seedHighLatencyMillis==100000); // Seeding's original branch is unconditional.
  });
  run("actual rate grace hysteresis exact boundaries and latency recovery",[]{
    auto p=read();auto f=download();f.state=QueueTorrentState::Downloading;f.runningMillis=30000;
    const auto initial=decide(p,f,context());CHECK(initial.activelyDownloading);f.activity=initial.activity;
    f.runningMillis=30001;
    auto d=decide(p,f,context(100001));CHECK(d.activelyDownloading);f.activity=d.activity;
    d=decide(p,f,context(110000));CHECK(d.activelyDownloading);f.activity=d.activity;
    d=decide(p,f,context(110001));CHECK(!d.activelyDownloading);
    f.activity=d.activity;f.diskWriteLatencyMillis=2501;d=decide(p,f,context(120001));f.activity=d.activity;
    d=decide(p,f,context(130001));CHECK(d.activelyDownloading);f.activity=d.activity;f.diskWriteLatencyMillis=0;
    d=decide(p,f,context(190001));CHECK(d.activelyDownloading);f.activity=d.activity;
    d=decide(p,f,context(190002));CHECK(d.activelyDownloading);f.activity=d.activity;
    d=decide(p,f,context(200002));CHECK(!d.activelyDownloading);
  });
  run("stalled download doesn't consume active slot but unknown rate does",[]{
    auto p=read();p.maximumDownloads=1;p.maximumActive=1;p.minimumDownloads=0;
    auto a=download(1,1),b=download(2,2);a.state=QueueTorrentState::Downloading;a.runningMillis=100000;
    auto r=plan(p,std::array{a,b});CHECK(r.decisions[0].action==QueueAction::Preserve&&r.decisions[1].action==QueueAction::Run);
    a.downloadBytesPerSecond.reset();r=plan(p,std::array{a,b});CHECK(!r.decisions[0].activityKnown&&r.decisions[1].action==QueueAction::Queue);
  });
  run("maximum timed cohort with and without a waiting replacement",[]{
    auto p=read();p.rankMode=QueueRankMode::Timed;p.maximumActive=0;
    p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;p.firstPriorityShareRatio=0;
    std::vector<QueueTorrentFacts> cohort;cohort.reserve(MaximumQueueTorrents);
    for(std::size_t i=0;i<MaximumQueueTorrents;++i){
      auto f=seed(i+1,static_cast<std::uint32_t>(i+1));f.state=QueueTorrentState::Seeding;
      f.runningMillis=600000;f.seedingMillis=600000;cohort.push_back(std::move(f));
    }
    const auto alone=plan(p,cohort);CHECK(alone.decisions.size()==MaximumQueueTorrents);
    for(const auto& d:alone.decisions){CHECK(d.reason!=QueueReason::TimedRotation);}
    cohort.front().state=QueueTorrentState::Queued;cohort.front().seedingMillis=0;
    const auto replaced=plan(p,cohort);bool rotated=false;
    for(const auto& d:replaced.decisions){rotated=rotated||d.reason==QueueReason::TimedRotation;}
    CHECK(rotated);
  });
  run("timed rotation uses actual elapsed and a real waiting alternative",[]{
    auto p=read();p.rankMode=QueueRankMode::Timed;p.maximumActive=1;
    auto a=seed(1,1),b=seed(2,2);a.state=QueueTorrentState::Seeding;a.runningMillis=400000;a.seedingMillis=300000;
    const auto r=plan(p,std::array{a,b});CHECK(r.decisions[0].seedingRank==1&&r.decisions[0].reason==QueueReason::TimedRotation);
    CHECK(r.decisions[0].action==QueueAction::Queue&&r.decisions[1].action==QueueAction::Run);
    CHECK(decide(p,a).action==QueueAction::Run);
    a.seedingMillis=299999;CHECK(decide(p,a).seedingRank==200000299);
    p.timedMinimumWithPeersSeconds=600;p.timedMaximumWithPeersSeconds=900;a.connectedLeechers=1;
    auto held=decide(p,a);CHECK(held.activity.timedMinimumMillis==840000);a.activity=held.activity;
    p.timedMaximumWithPeersSeconds=700;CHECK(decide(p,a).activity.timedMinimumMillis==840000);
  });
  run("known zero autostart bypasses ignore rules without fabricated scrape",[]{
    auto p=read();p.autoStartZeroPeers=true;p.ignoreShareRatio=1000;auto f=seed();f.swarm=QueueSwarm{3,0,true};
    CHECK(decide(p,f).action==QueueAction::Run);f.swarm.reset();CHECK(decide(p,f).action==QueueAction::Queue);
    f.swarm=QueueSwarm{3,0,false};CHECK(decide(p,f).action==QueueAction::Queue);
  });
  run("actual upload saturation threshold is strict and only prevents new seeds",[]{
    auto p=read();p.preventNewSeedsAtUploadLimit=true;auto f=seed();auto c=context();c.uploadLimitBytesPerSecond=100*1024;
    c.smoothedUploadBytesPerSecond=95*1024;CHECK(decide(p,f,c).action==QueueAction::Run);
    c.smoothedUploadBytesPerSecond=96*1024;CHECK(decide(p,f,c).reason==QueueReason::UploadLimit);
    c.smoothedUploadBytesPerSecond.reset();CHECK(decide(p,f,c).reason!=QueueReason::UploadLimit);
  });
  run("sorting missing observations remains deterministic and inputs unchanged",[]{
    auto p=read();p.maximumDownloads=1;p.minimumDownloads=0;p.downloadOrder=QueueDownloadOrder::Size;
    auto a=download(1,1),b=download(2,2),c=download(3,3);a.sizeBytes=10;c.sizeBytes=20;
    const auto r=plan(p,std::array{a,b,c});CHECK(r.decisions[2].action==QueueAction::Run);
    CHECK(a.sizeBytes==10&&!b.sizeBytes&&c.sizeBytes==20);
    p.downloadOrder=QueueDownloadOrder::FilePriority;a.filePriority=std::vector<std::int64_t>{0,2,0,1};
    c.filePriority=std::vector<std::int64_t>{0,2,0,3};CHECK(plan(p,std::array{a,b,c}).decisions[2].action==QueueAction::Run);
    p.downloadOrder=QueueDownloadOrder::Speed;CHECK(plan(p,std::array{a,b,c}).unavailableInputs&16U);
  });
  run("stalled seed cap zero means999 and valid-zero exemption is explicit",[]{
    auto p=read();p.maximumActive=1;p.maximumStalledSeeds=1;
    auto a=seed(1,1),b=seed(2,2),c=seed(3,3);
    for(auto* f:{&a,&b,&c}){f->state=QueueTorrentState::Seeding;f->runningMillis=1000000;}
    auto r=plan(p,std::array{a,b,c});CHECK(r.decisions[0].action==QueueAction::Preserve);
    CHECK(r.decisions[1].reason==QueueReason::StalledLimit&&r.decisions[2].reason==QueueReason::StalledLimit);
    p.maximumStalledSeeds=0;r=plan(p,std::array{a,b,c});for(const auto& d:r.decisions){CHECK(d.action==QueueAction::Preserve);}
    p.maximumStalledSeeds=1;p.ignoreZeroPeers=false;
    for(auto* f:{&a,&b,&c}){f->swarm=QueueSwarm{1,0,true};}
    r=plan(p,std::array{a,b,c});for(const auto& d:r.decisions){CHECK(d.action==QueueAction::Preserve);}
  });
  run("invalid identity time and limits fail without a prefix plan",[]{
    auto p=read();const auto empty=evaluateQueue(p,{},{});CHECK(empty&&empty->decisions.empty());
    auto f=download();CHECK(!evaluateQueue(p,std::array{f,f},context()));
    f.token=0;CHECK(!evaluateQueue(p,std::span(&f,1),context()));f.token=1;
    f.activity.lastObservationMillis=100001;CHECK(!evaluateQueue(p,std::span(&f,1),context()));f.activity={};
    f.swarm=QueueSwarm{-1,0,true};CHECK(!evaluateQueue(p,std::span(&f,1),context()));f.swarm.reset();
    auto bad=context();bad.uploadLimitBytesPerSecond=std::numeric_limits<std::int64_t>::max();CHECK(!evaluateQueue(p,std::span(&f,1),bad));
    std::vector<QueueTorrentFacts> many(MaximumQueueTorrents+1);const auto limited=evaluateQueue(p,many,context());
    CHECK(!limited&&limited.error().code==QueuePolicyErrorCode::Limit);
    p.rankMode=static_cast<QueueRankMode>(99);CHECK(!evaluateQueue(p,std::span(&f,1),context()));
  });
  run("actual source share ratio wraps long then narrows int without floating clamp",[]{
    CHECK(sourceQueueShareRatio(1000,1250,0,0)==1250);
    CHECK(sourceQueueShareRatio(100,1,100,0)==-1);
    CHECK(sourceQueueShareRatio(1,std::numeric_limits<std::int64_t>::max(),0,0)==-1000);
    CHECK(sourceQueueShareRatio(2,std::numeric_limits<std::int64_t>::max(),0,0)==-500);
    CHECK(sourceQueueShareRatio(1,2147484,0,0)==std::numeric_limits<std::int32_t>::max());
    CHECK(sourceQueueShareRatio(std::numeric_limits<std::int64_t>::max(),1,
        std::numeric_limits<std::int64_t>::max(),1)==-1);
    auto f=seed();f.shareRatio=-500;CHECK(decide(read(),f).firstPriority);
  });
  run("raw last scrape terminal is live selected source and never aggregate cache or response authority",[]{
    std::array<unsigned char,32> tracker{};tracker[0]=7;
    QueueOwnerObservations source(1);CHECK(source.trackerOrder(std::span(&tracker,1)));CHECK(source.restoreAggregate("3,99,88"));
    CHECK(!source.facts(1).lastTrackerScrape);
    std::uint64_t sequence=0;
    const auto send=[&](QueueObservationKind kind,unsigned protocol,std::int64_t now,std::int32_t seeds=-1,std::int32_t peers=-1){
      QueueObservation e;e.sequence=++sequence;e.request=sequence;e.observedNanos=now*1000000;e.requestStartedNanos=(now-10)*1000000;
      e.observedWallMillis=100000+now;e.requestStartedWallMillis=100000+now-10;e.kind=kind;e.protocol=protocol;e.tracker=tracker;e.seeds=seeds;e.peers=peers;return source.observe(e);
    };
    CHECK(send(QueueObservationKind::Announce,1,1000,5,6));CHECK(!source.facts(1000).lastTrackerScrape);
    CHECK(send(QueueObservationKind::ScrapeFailed,1,2000));auto f=source.facts(2000);CHECK(f.lastTrackerScrape);
    const auto first=*f.lastTrackerScrape;CHECK(first.kind==QueueScrapeTerminalKind::RequestFailed&&first.sequence==2&&first.request==2);
    CHECK(first.protocol==1&&first.tracker==tracker&&first.seeds==-1&&first.peers==-1);
    CHECK(first.observedNanos==2000000000&&first.requestStartedNanos==1990000000);
    CHECK(first.observedWallMillis==102000&&first.requestStartedWallMillis==101990);
    CHECK(send(QueueObservationKind::Scrape,2,3000,99,99));CHECK(source.facts(3000).lastTrackerScrape->request==first.request);
    CHECK(send(QueueObservationKind::Scrape,1,4000,7,9));f=source.facts(4000);CHECK(f.lastTrackerScrape&&f.lastTrackerScrape->kind==QueueScrapeTerminalKind::Reply&&f.lastTrackerScrape->seeds==7&&f.lastTrackerScrape->peers==9);
    CHECK(send(QueueObservationKind::ScrapeFailed,1,5000));f=source.facts(5000);CHECK(f.lastTrackerScrape&&f.lastTrackerScrape->kind==QueueScrapeTerminalKind::RequestFailed);
    CHECK(f.lastTrackerScrape->seeds==-1&&f.lastTrackerScrape->peers==-1);CHECK(f.swarm&&f.swarm->success); // Historical counts do not turn RPC error into original RT_ERROR.
    tracker[0]=8;CHECK(send(QueueObservationKind::Scrape,1,6000,1,2));CHECK(!source.facts(6000).lastTrackerScrape); // Unknown URL withdraws latest raw source.
    tracker[0]=7;CHECK(send(QueueObservationKind::Scrape,1,7000,0,0));f=source.facts(7000);CHECK(f.lastTrackerScrape&&f.lastTrackerScrape->seeds==0&&f.lastTrackerScrape->peers==0);
    source.lost();CHECK(!source.facts(7000).lastTrackerScrape);
    QueueOwnerObservations readmitted(1);CHECK(readmitted.trackerOrder(std::span(&tracker,1)));readmitted.restoreLegacy((std::int64_t{7}<<32)|9);CHECK(!readmitted.facts(8000).lastTrackerScrape);
  });
  run("original single tracker response distinguishes missing unknown valid and retained errors",[]{
    const auto absent=sourceQueueOriginalScrapeResponse({});
    CHECK(absent.type==QueueOriginalScrapeType::Error&&!absent.content);
    CHECK(sourceQueueQueuedScrapeEligible(absent,{}));
    const auto invalid=sourceQueueOriginalScrapeResponse(QueueOriginalScrapeContent{-1,-1,17});
    CHECK(invalid.type==QueueOriginalScrapeType::Error&&sourceQueueQueuedScrapeEligible(invalid,18));
    const auto valid=sourceQueueOriginalScrapeResponse(QueueOriginalScrapeContent{0,0,17});
    CHECK(valid.type==QueueOriginalScrapeType::Success&&!sourceQueueQueuedScrapeEligible(valid,{}));
    CHECK(sourceQueueQueuedScrapeEligible(valid,17)&&!sourceQueueQueuedScrapeEligible(valid,18));
    CHECK(sourceQueueOriginalScrapeResponse(QueueOriginalScrapeContent{-2,-1,17}).type==QueueOriginalScrapeType::Success); // Literal isValid, no invented nonnegative clamp.
    CHECK(!sourceQueueQueuedScrapeEligible(QueueOriginalScrapeResponse{},-1));
    std::array<unsigned char,32> key{};key[0]=23;QueueOwnerObservations source(1);CHECK(source.trackerOrder(std::span(&key,1)));
    CHECK(source.restoreAggregate("3,9,8"));CHECK(!source.facts(1).singleTrackerResponse);
    std::uint64_t sequence=0;auto now=std::int64_t{1000};
    const auto send=[&](QueueObservationKind kind,int seeds=-1,int peers=-1,unsigned protocol=1){
      QueueObservation e;e.sequence=++sequence;e.request=sequence;e.observedNanos=++now*1000000;e.requestStartedNanos=1;
      e.observedWallMillis=100000+now;e.requestStartedWallMillis=100000;e.kind=kind;e.protocol=protocol;e.tracker=key;e.seeds=seeds;e.peers=peers;return source.observe(e);
    };
    CHECK(send(QueueObservationKind::ScrapeFailed));auto facts=source.facts(now);CHECK(facts.singleTrackerResponse);
    CHECK(facts.singleTrackerResponse->response.type==QueueOriginalScrapeType::Unknown);
    CHECK(!sourceQueueQueuedScrapeEligible(facts.singleTrackerResponse->response,{}));
    CHECK(send(QueueObservationKind::Scrape,3,4));CHECK(source.facts(now).singleTrackerResponse->response.type==QueueOriginalScrapeType::Unknown); // downloaded presence/value absent from ABI.
    CHECK(send(QueueObservationKind::Announce,5,6));facts=source.facts(now);const auto known=*facts.singleTrackerResponse;
    CHECK(known.response.type==QueueOriginalScrapeType::Success&&known.response.content&&known.response.content->seeds==5&&known.response.content->peers==6);
    CHECK(known.response.content->startedWallMillis==100000+now);
    CHECK(send(QueueObservationKind::ScrapeFailed));CHECK(send(QueueObservationKind::AnnounceFailed));facts=source.facts(now);
    CHECK(facts.singleTrackerResponse->response.type==QueueOriginalScrapeType::Success);
    CHECK(facts.singleTrackerResponse->response.content->startedWallMillis==known.response.content->startedWallMillis);
    CHECK(facts.singleTrackerResponse->response.content->seeds==5); // Actual failure never erases retained response.
    CHECK(send(QueueObservationKind::Scrape,7,8));facts=source.facts(now);CHECK(facts.singleTrackerResponse->response.type==QueueOriginalScrapeType::Success&&!facts.singleTrackerResponse->response.content);
    CHECK(!sourceQueueQueuedScrapeEligible(facts.singleTrackerResponse->response,known.response.content->startedWallMillis));
    CHECK(send(QueueObservationKind::Announce,2,3));const auto selected=source.facts(now).singleTrackerResponse->sequence;
    CHECK(send(QueueObservationKind::Announce,99,99,2));CHECK(source.facts(now).singleTrackerResponse->sequence==selected);
    CHECK(send(QueueObservationKind::Announce,-1,-1));CHECK(source.facts(now).singleTrackerResponse->response.type==QueueOriginalScrapeType::Success&&!source.facts(now).singleTrackerResponse->response.content);
    key[0]=24;CHECK(send(QueueObservationKind::ScrapeFailed));CHECK(!source.facts(now).singleTrackerResponse);
    source.lost();CHECK(!source.facts(now).singleTrackerResponse);
    QueueOwnerObservations readmitted(1);CHECK(readmitted.trackerOrder(std::span(&key,1)));CHECK(!readmitted.facts(now).singleTrackerResponse);
    std::array<std::array<unsigned char,32>,2> keys{};keys[1][0]=1;QueueOwnerObservations multiple(1);CHECK(multiple.trackerOrder(keys));
    QueueObservation e;e.kind=QueueObservationKind::Announce;e.sequence=1;e.request=1;e.observedNanos=1000000;e.requestStartedNanos=1;e.protocol=1;e.seeds=1;e.peers=2;CHECK(multiple.observe(e));CHECK(!multiple.facts(1).singleTrackerResponse);
  });
  run("library tracker response defaults actual terminals backups and manual lifetime match original",[]{
    QueueOriginalTrackerResponse owner;CHECK(owner.response().type==QueueOriginalScrapeType::Error);
    CHECK(!owner.eligible(0)&&owner.regularDue(0));CHECK(!owner.publishDht(1,2,10));
    owner.requested();CHECK(!owner.regularDue(1)&&!owner.eligible(1));
    QueueTrackerTerminal event;event.kind=QueueTrackerTerminalKind::ScrapeFailed;event.definitiveFailure=true;event.observedMillis=1000;
    owner.observe(event);CHECK(owner.eligible(1000)&&!owner.regularDue(1000));CHECK(owner.response().type==QueueOriginalScrapeType::Error);
    CHECK(owner.publishDht(2,3,5000));CHECK(owner.dhtBackup()&&owner.eligible(2000));CHECK(owner.response().type==QueueOriginalScrapeType::Success);
    CHECK(owner.response().content->startedWallMillis==5000);CHECK(!owner.eligible(601000)&&owner.regularDue(601000));
    owner.requested();CHECK(!owner.eligible(601000));event.observedMillis=602000;owner.observe(event);CHECK(owner.eligible(602000));
    // Wrapper TrackedTorrent removal loses own-start equality but the shared
    // original BT response and its counts remain. Requeue is not a null reset.
    owner.forgetDhtAuthority();CHECK(!owner.eligible(602000));CHECK(owner.response().content->seeds==2);
    event.observedMillis=603000;owner.observe(event);CHECK(!owner.eligible(603000));CHECK(owner.response().type==QueueOriginalScrapeType::Success);
    QueueOriginalTrackerResponse fresh;event.kind=QueueTrackerTerminalKind::Scrape;event.comparable=true;event.definitiveFailure=false;event.present=1;event.complete=7;event.requestStartedWallMillis=6000;event.observedMillis=4000;
    fresh.observe(event);CHECK(fresh.response().type==QueueOriginalScrapeType::Success);CHECK(fresh.response().content->seeds==7&&fresh.response().content->peers==0);CHECK(!fresh.eligible(4000));
    event.kind=QueueTrackerTerminalKind::ScrapeFailed;event.definitiveFailure=true;event.observedMillis=5000;fresh.observe(event);CHECK(fresh.response().content->seeds==7&&!fresh.eligible(5000));
    event.kind=QueueTrackerTerminalKind::Scrape;event.comparable=true;event.present=7;event.complete=9;event.incomplete=10;event.downloaded=-2;event.observedMillis=6000;fresh.observe(event);CHECK(fresh.response().content->seeds==7); // Invalid reply retains counts.
    event.kind=QueueTrackerTerminalKind::Announce;event.present=4;event.downloaded=12;event.observedWallMillis=7000;event.observedMillis=7000;fresh.observe(event);CHECK(fresh.response().content->seeds==0&&fresh.response().content->peers==0); // Downloaded-only creates0/0.
    event.present=3;event.complete=4294967296LL;event.incomplete=2;event.observedMillis=8000;fresh.observe(event);CHECK(fresh.response().content->seeds==0&&fresh.response().content->peers==2); // Java long->int.
    event.present=0;event.observedMillis=9000;fresh.observe(event);CHECK(fresh.response().content->peers==2); // No count fields leaves response alone.
    event.kind=QueueTrackerTerminalKind::Scrape;event.comparable=false;event.observedMillis=10000;fresh.observe(event);CHECK(fresh.response().type==QueueOriginalScrapeType::Unknown&&!fresh.eligible(10000));
    event.kind=QueueTrackerTerminalKind::ScrapeFailed;event.definitiveFailure=true;event.observedMillis=11000;fresh.observe(event);CHECK(fresh.response().type==QueueOriginalScrapeType::Unknown); // Failure cannot repair lost lineage.
    event.kind=QueueTrackerTerminalKind::Scrape;event.comparable=true;event.present=3;event.complete=0;event.incomplete=0;event.observedMillis=12000;fresh.observe(event);CHECK(fresh.response().type==QueueOriginalScrapeType::Success&&!fresh.eligible(12000));
  });
  run("original multi tracker selection preserves status retained counts backup and Java tier shuffle",[]{
    const std::array<unsigned,5> tiers{0,0,0,1,1};
    CHECK(sourceQueueTrackerOrder(tiers,0)==std::vector<std::size_t>({1,2,0,3,4}));
    CHECK(sourceQueueTrackerOrder(tiers,1)==std::vector<std::size_t>({2,1,0,3,4}));
    CHECK(sourceQueueTrackerOrder(tiers,0)==sourceQueueTrackerOrder(tiers,std::int64_t{1}<<48)); // Java uses only48 seed bits.
    CHECK(sourceQueueTrackerOrder(std::array<unsigned,2>{1,0},0).empty());
    std::array<QueueOriginalTrackerResponse,3> responses;
    const std::array<const QueueOriginalTrackerResponse*,3> rows{&responses[0],&responses[1],&responses[2]};
    CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{0});
    QueueTrackerTerminal error;error.kind=QueueTrackerTerminalKind::ScrapeFailed;error.definitiveFailure=true;error.observedMillis=100;
    responses[0].requested();responses[0].observe(error);CHECK(responses[0].eligible(100));
    CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{1}); // First error is not all failed.
    responses[1].requested();CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{1});
    QueueTrackerTerminal good;good.kind=QueueTrackerTerminalKind::Scrape;good.comparable=true;good.present=7;good.complete=5;good.incomplete=6;good.downloaded=9;good.observedMillis=200;good.requestStartedWallMillis=900;
    good.observedMillis=500;responses[1].observe(good);CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{1}); // Later initial never replaces healthy earlier.
    CHECK(!responses[1].regularDue(600500));CHECK(!responses[1].regularPollDue(600499));CHECK(responses[1].regularPollDue(600500));
    error.observedMillis=300;responses[1].observe(error);CHECK(responses[1].response().type==QueueOriginalScrapeType::Success); // Original ERROR retains valid response content.
    CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{2});responses[2].observe(error);
    CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{0}); // All actual errors selects first error.
    CHECK(responses[0].publishDht(2,3,1000));CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{0});
    good.observedMillis=400;responses[2].observe(good);CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{2}); // Prefer real tracker over DHT backup.
    responses[1].unavailable();CHECK(!sourceQueueTrackerSelection(rows)); // No local failure/unknown as complete failure.
    good.observedMillis=500;responses[1].observe(good);CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{1});
    responses[0].requested();CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{0});responses[0].cancelRequest();
    CHECK(sourceQueueTrackerSelection(rows)==std::optional<std::size_t>{1});
  });
  run("source lifecycle floors each actual peer epoch and resets seed time on unfinish",[]{
    QueueOwnerObservations source(1);std::uint64_t sequence=0;
    const auto send=[&](QueueObservationKind kind,std::int64_t millis){QueueObservation e;e.sequence=++sequence;e.observedNanos=millis*1000000;e.observedWallMillis=100000+millis;e.kind=kind;return source.observe(e);};
    CHECK(send(QueueObservationKind::StartedDownload,1001));CHECK(send(QueueObservationKind::Finished,2500));
    auto f=source.facts(3999);CHECK(f.durations&&f.durations->downloading==1&&f.durations->seeding==1);
    CHECK(send(QueueObservationKind::Stopped,3999));CHECK(send(QueueObservationKind::StartedDownload,4500));CHECK(send(QueueObservationKind::Stopped,4999));
    f=source.facts(5000);CHECK(f.durations&&f.durations->downloading==1&&f.durations->seeding==1);
    CHECK(send(QueueObservationKind::StartedDownload,6000));CHECK(send(QueueObservationKind::Finished,6999));CHECK(send(QueueObservationKind::Unfinished,7000));
    f=source.facts(7999);CHECK(f.durations&&f.durations->downloading==2&&f.durations->seeding==1);
    CHECK(!send(QueueObservationKind::Lost,8000));CHECK(!source.facts(8000).durations&&!source.facts(8000).swarm);
  });
  run("retained scrape and announce choose each field with exact request-start wall source",[]{
    QueueOwnerObservations source(1);std::array<unsigned char,32> key{};key[0]=1;CHECK(source.trackerOrder(std::span(&key,1)));
    std::uint64_t sequence=0;
    const auto send=[&](QueueObservationKind kind,int seeds,int peers,std::int64_t start){QueueObservation e;e.sequence=++sequence;e.observedNanos=(1000+static_cast<std::int64_t>(sequence))*1000000;e.observedWallMillis=200000+static_cast<std::int64_t>(sequence);e.kind=kind;e.seeds=seeds;e.peers=peers;
      if(static_cast<unsigned>(kind)>=6){e.protocol=1;e.request=sequence;e.requestStartedNanos=1000000;e.requestStartedWallMillis=start;e.tracker=key;}return source.observe(e);};
    CHECK(send(QueueObservationKind::StartedSeed,-1,-1,0));CHECK(send(QueueObservationKind::Announce,7,9,200002));
    CHECK(send(QueueObservationKind::Scrape,1,0,200002));auto f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==7&&f.swarm->peers==9&&f.swarm->success);
    CHECK(send(QueueObservationKind::Scrape,1,0,200001));f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==1&&f.swarm->peers==9);
    CHECK(send(QueueObservationKind::Scrape,4,6,200002));CHECK(send(QueueObservationKind::Announce,70,90,200002));f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==3&&f.swarm->peers==6);
    CHECK(send(QueueObservationKind::Scrape,1,0,200002));CHECK(send(QueueObservationKind::AnnounceFailed,-1,-1,200002));f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==0&&f.swarm->peers==0&&f.swarm->success);
  });
  run("original configured order owns equal-count scrape ties not reply arrival order",[]{
    QueueOwnerObservations source(2);std::array<std::array<unsigned char,32>,2> keys{};keys[0][0]=1;keys[1][0]=2;CHECK(source.trackerOrder(keys));
    QueueObservation e;e.sequence=1;e.observedNanos=1000000000;e.observedWallMillis=200000;e.kind=QueueObservationKind::StartedSeed;CHECK(source.observe(e));
    e.kind=QueueObservationKind::Scrape;e.protocol=2;e.seeds=4;e.peers=7;e.requestStartedNanos=1000000;e.requestStartedWallMillis=200001;e.tracker=keys[1];e.request=1;e.sequence=2;e.observedNanos=1100000000;CHECK(source.observe(e));
    e.tracker=keys[0];e.request=2;e.sequence=3;e.observedNanos=1200000000;e.requestStartedWallMillis=199999;CHECK(source.observe(e));
    const auto f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==4&&f.swarm->peers==7);
    e.protocol=1;e.sequence=4;e.request=3;e.seeds=999;e.observedNanos=1300000000;CHECK(source.observe(e));CHECK(source.facts(2000).swarm->seeds==4);
  });
  run("original aggregate cache and startup-success remain distinct from actual network success",[]{
    QueueOwnerObservations source(1);CHECK(source.trackerOrder({}));CHECK(source.restoreAggregate("1,2,3"));
    QueueObservation e;e.sequence=1;e.observedNanos=1000000000;e.observedWallMillis=200000;e.kind=QueueObservationKind::StartedSeed;CHECK(source.observe(e));
    auto f=source.facts(2000);CHECK(f.swarm&&f.swarm->seeds==2&&f.swarm->peers==3&&f.swarm->success&&f.swarm->startupSuccess);
    QueueOwnerObservations legacy(1);legacy.restoreLegacy((std::int64_t{5}<<32)|7);f=legacy.facts(2000);CHECK(f.swarm&&f.swarm->seeds==0&&f.swarm->peers==0&&f.swarm->success);
    QueueOwnerObservations empty(1);f=empty.facts(2000);CHECK(f.swarm&&!f.swarm->success&&f.swarm->startupSuccess);
    auto torrent=seed();torrent.swarm=QueueSwarm{0,0,false,true};auto c=context(1000);CHECK(decide(read(),torrent,c).reason!=QueueReason::WaitingForScrape);
  });
  run("duplicate finished keeps first source epoch and late scrape keeps request start",[]{
    QueueOwnerObservations source(1);std::array<unsigned char,32> key{};key[0]=7;
    CHECK(source.trackerOrder(std::span(&key,1)));
    QueueObservation e;e.sequence=1;e.observedNanos=1000000000;e.observedWallMillis=100000;e.kind=QueueObservationKind::StartedDownload;CHECK(source.observe(e));
    e.sequence=2;e.observedNanos=2000000000;e.observedWallMillis=101000;e.kind=QueueObservationKind::Finished;CHECK(source.observe(e));
    e.sequence=3;e.observedNanos=3000000000;e.observedWallMillis=102000;CHECK(source.observe(e));
    const auto active=source.facts(3500);CHECK(active.durations&&active.durations->downloading==1&&active.durations->seeding==1);
    CHECK(active.seedingMillis==1500);
    e.sequence=4;e.observedNanos=4000000000;e.observedWallMillis=103000;e.kind=QueueObservationKind::Stopped;CHECK(source.observe(e));
    e.sequence=5;e.observedNanos=5000000000;e.observedWallMillis=104000;e.kind=QueueObservationKind::Scrape;
    e.protocol=1;e.request=11;e.requestStartedNanos=1500000000;e.requestStartedWallMillis=100500;e.seeds=3;e.peers=4;e.tracker=key;CHECK(source.observe(e));
    const auto stopped=source.facts(5000);CHECK(stopped.durations&&stopped.durations->downloading==1&&stopped.durations->seeding==2);
    CHECK(stopped.swarm&&stopped.swarm->seeds==3&&stopped.swarm->peers==4);
    CHECK(!stopped.runningMillis&&!stopped.seedingMillis);
  });
  run("wrapped original elapsed remains known and skipped rather than missing",[]{
    auto p=read();p.firstPriorityShareRatio=0;p.firstPrioritySeedMinutes=1;p.firstPriorityActiveMinutes=1;
    auto f=seed();f.secondsDownloading=std::numeric_limits<std::int64_t>::max();f.secondsSeeding=1;
    p.firstPriorityMode=QueueFirstPriorityMode::Any;CHECK(decide(p,f).firstPriority);
    f.secondsSeeding=-2;f.secondsDownloading=-1;CHECK(!decide(p,f).firstPriority);
    p.firstPriorityMode=QueueFirstPriorityMode::All;f.shareRatio=-500;CHECK(decide(p,f).firstPriority);
    f.secondsSeeding.reset();CHECK(!decide(p,f).firstPriority);
  });
  run("original self exclusion requires positive seed-start wall time",[]{
    for(const auto wall:std::array<std::int64_t,2>{0,-1}){
      QueueOwnerObservations source(1);std::array<unsigned char,32> key{};key[0]=1;CHECK(source.trackerOrder(std::span(&key,1)));
      QueueObservation e;e.sequence=1;e.observedNanos=1000000000;e.observedWallMillis=wall;e.kind=QueueObservationKind::StartedSeed;CHECK(source.observe(e));
      e.sequence=2;e.observedNanos=2000000000;e.observedWallMillis=1000;e.kind=QueueObservationKind::Scrape;e.protocol=1;e.request=1;e.requestStartedNanos=1;e.requestStartedWallMillis=1;e.seeds=2;e.peers=3;e.tracker=key;CHECK(source.observe(e));
      const auto observed=source.facts(2000);CHECK(observed.swarm&&observed.swarm->seeds==2&&observed.swarm->peers==3);
    }
  });
  run("idle age preserves saved carryover and known never-upload sentinel",[]{
    QueueIdleUploadSample sample;sample.admission=1;sample.sequence=1;sample.epoch={1,100000,{}};sample.nowWallMillis=105900;
    QueueIdleUploadAge saved(10);auto first=saved.observe(sample);CHECK(first.known&&first.seconds==15);
    sample.sequence=2;sample.nowWallMillis=106000;const auto next=saved.observe(sample);CHECK(next.known&&next.seconds==16);
    QueueIdleUploadAge never;sample.sequence=1;const auto missing=never.observe(sample);CHECK(missing.known&&missing.seconds==-1);
  });
  run("actual payload idle resets backward wall time once and preserves subsequent age",[]{
    QueueIdleUploadAge idle;QueueIdleUploadSample sample;sample.admission=1;sample.sequence=1;sample.epoch={1,100000,{}};
    sample.nowWallMillis=106000;sample.rawPayloadSent=20;sample.cumulativePayloadSent=20;sample.lastPayloadWallMillis=104000;
    auto value=idle.observe(sample);CHECK(value.known&&value.seconds==2);
    sample.sequence=2;sample.nowWallMillis=103000;value=idle.observe(sample);CHECK(value.known&&value.seconds==0);
    sample.sequence=3;sample.nowWallMillis=105000;value=idle.observe(sample);CHECK(value.known&&value.seconds==2);
  });
  run("accepted stop freezes actual idle and resumes known carryover without paused elapsed",[]{
    QueueIdleUploadAge idle;QueueIdleUploadSample sample;sample.admission=1;sample.sequence=1;sample.epoch={1,100000,{}};
    sample.nowWallMillis=105000;sample.rawPayloadSent=20;sample.cumulativePayloadSent=20;sample.lastPayloadWallMillis=104000;CHECK(idle.observe(sample).seconds==1);
    sample.sequence=2;sample.epoch.stoppedWallMillis=106000;sample.nowWallMillis=109000;auto value=idle.observe(sample);CHECK(value.known&&value.seconds==2);
    sample.sequence=3;sample.nowWallMillis=119000;value=idle.observe(sample);CHECK(value.known&&value.seconds==2);
    sample.sequence=4;sample.epoch={2,120000,{}};sample.nowWallMillis=123000;sample.rawPayloadSent=0;value=idle.observe(sample);CHECK(value.known&&value.seconds==5);
    sample.sequence=5;sample.nowWallMillis=124000;sample.rawPayloadSent=5;sample.cumulativePayloadSent=25;sample.lastPayloadWallMillis=123000;value=idle.observe(sample);CHECK(value.known&&value.seconds==1);
  });
  run("missed or changed stop payload stays unavailable until genuine new epoch activity",[]{
    QueueIdleUploadAge idle(5);QueueIdleUploadSample sample;sample.admission=1;sample.sequence=1;sample.epoch={2,120000,{}};
    sample.nowWallMillis=130000;sample.cumulativePayloadSent=20;CHECK(!idle.observe(sample).known);
    sample.sequence=2;sample.rawPayloadSent=1;sample.cumulativePayloadSent=21;sample.lastPayloadWallMillis=129000;auto value=idle.observe(sample);CHECK(value.known&&value.seconds==1);
    sample.sequence=3;sample.epoch.stoppedWallMillis=131000;sample.rawPayloadSent=2;sample.cumulativePayloadSent=22;sample.lastPayloadWallMillis=130000;CHECK(!idle.observe(sample).known);
    sample.sequence=4;sample.epoch={3,140000,{}};sample.nowWallMillis=142000;sample.rawPayloadSent=0;CHECK(!idle.observe(sample).known);
    sample.sequence=5;sample.rawPayloadSent=1;sample.cumulativePayloadSent=23;sample.lastPayloadWallMillis=141000;value=idle.observe(sample);CHECK(value.known&&value.seconds==1);
  });
  run("known negative idle wrap differs from unavailable source and stale samples",[]{
    QueueIdleUploadAge idle(std::numeric_limits<std::int32_t>::max());QueueIdleUploadSample sample;sample.admission=1;sample.sequence=1;sample.epoch={1,100000,{}};sample.nowWallMillis=102000;
    auto value=idle.observe(sample);CHECK(value.known&&value.seconds==-2147483647);
    CHECK(!idle.observe(sample).known);sample.sequence=2;CHECK(idle.observe(sample).known);
    auto p=read();p.firstPriorityIgnoreIdleMinutes=1;auto f=seed();f.shareRatio=1;f.secondsSeeding=10;f.secondsSinceLastUpload.reset();
    f.idleUploadSourceKnown=true;CHECK(decide(p,f).firstPriorityKnown&&decide(p,f).firstPriority);
    f.idleUploadSourceKnown=false;CHECK(!decide(p,f).firstPriorityKnown);
  });
  std::cout<<families<<" queue-policy families, "<<assertions<<" assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
