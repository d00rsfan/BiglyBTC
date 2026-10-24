#include "biglybtc/queue_policy.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <set>
#include <string_view>

namespace biglybtc::core {
namespace {
using Error = QueuePolicyError;
using Code = QueuePolicyErrorCode;
struct Invalid { Error error; };
[[noreturn]] void fail(Code code, std::string_view key) { throw Invalid{{code,std::string(key)}}; }
constexpr auto Max = std::numeric_limits<std::int32_t>::max();
constexpr auto MaxSeconds = Max/1000;
struct Reader {
  const Document* document;
  const Node* find(std::string_view key) const {
    if (!document) { return nullptr; }
    const Node* result = nullptr;
    const auto& children = document->root().children;
    for (std::size_t i=0;i<children.size();i+=2) {
      if (children[i].bytes == key) {
        if (result) { fail(Code::InvalidConfiguration,key); }
        result=&children[i+1];
      }
    }
    return result;
  }
  std::int32_t integer(std::string_view key, std::int32_t fallback, std::int32_t maximum=Max) const {
    const auto* n=find(key); if(!n){return fallback;}
    if(n->type!=Node::Type::Integer||n->integer<0||n->integer>maximum){fail(Code::InvalidConfiguration,key);}
    return static_cast<std::int32_t>(n->integer);
  }
  bool boolean(std::string_view key,bool fallback) const {
    return integer(key,fallback?1:0,1)!=0;
  }
  std::int32_t stopRatio() const {
    constexpr std::string_view key="Stop Ratio";
    const auto* n=find(key);if(!n){return 0;}
    float value=0;
    if(n->type==Node::Type::Integer){value=static_cast<float>(n->integer);}
    else if(n->type==Node::Type::Bytes&&n->bytes.size()<=64){
      auto text=n->bytes;
      while(!text.empty()&&static_cast<unsigned char>(text.front())<=32){text.remove_prefix(1);}
      while(!text.empty()&&static_cast<unsigned char>(text.back())<=32){text.remove_suffix(1);}
      if(!text.empty()&&text.front()=='+'){text.remove_prefix(1);}
      const auto read=std::from_chars(text.data(),text.data()+text.size(),value);
      if(text.empty()||read.ec!=std::errc{}||read.ptr!=text.data()+text.size()){fail(Code::InvalidConfiguration,key);}
    }else{fail(Code::InvalidConfiguration,key);}
    const float scaled=1000.0F*value;
    if(!std::isfinite(scaled)||value<0||scaled>=2147483648.0F){fail(Code::InvalidConfiguration,key);}
    return static_cast<std::int32_t>(scaled);
  }
};
bool enumValid(const QueuePolicy& p) {
  return static_cast<unsigned>(p.rankMode)<=4&&static_cast<unsigned>(p.downloadOrder)<=8
      &&static_cast<unsigned>(p.firstPriorityMode)<=1;
}
void validatePolicy(const QueuePolicy& p) {
  if(!enumValid(p)){fail(Code::InvalidConfiguration,"queue enum");}
  const std::array nonnegative{p.maximumDownloads,p.maximumActive,p.minimumDownloads,p.maximumActiveWhenSeeding,
    p.seedCountFallback,p.minimumPeersToBoostNoSeeds,p.minimumDownloadBytesPerSecond,p.minimumSeedBytesPerSecond,
    p.maximumStalledSeeds,p.firstPriorityShareRatio,p.firstPriorityIgnoreSeedPeerRatio,p.firstPriorityIgnoreIdleMinutes,
    p.ignoreShareRatio,p.ignoreShareRatioSeedStart,p.ignoreSeedCount,p.ignorePeerSeedRatio,p.ignorePeerSeedRatioSeedStart,
    p.peersAsFullCopy,p.fakeFullCopySeedStart,p.uploadLimitSlack,p.reservedLightSeedSlots,p.flexibleSeedSlots};
  for(const auto n:nonnegative){if(n<0){fail(Code::InvalidConfiguration,"negative queue setting");}}
  if(p.minimumSeedSeconds<0||p.minimumSeedSeconds>MaxSeconds||p.minimumSeedActiveSeconds<0||p.minimumSeedActiveSeconds>MaxSeconds
      ||p.timedMinimumWithPeersSeconds<0||p.timedMinimumWithPeersSeconds>MaxSeconds
      ||p.timedMaximumWithPeersSeconds<0||p.timedMaximumWithPeersSeconds>MaxSeconds
      ||p.firstPrioritySeedMinutes<0||p.firstPrioritySeedMinutes>Max/60
      ||p.firstPriorityActiveMinutes<0||p.firstPriorityActiveMinutes>Max/60){fail(Code::InvalidConfiguration,"queue duration");}
}
bool absentOrNonnegative(const std::optional<std::int64_t>& x){return !x||*x>=0;}
void validateFacts(std::span<const QueueTorrentFacts> facts,const QueueContext& c) {
  if(facts.size()>MaximumQueueTorrents){fail(Code::Limit,"queue torrents");}
  if(c.nowMillis<0||c.ownerStartedMillis<0||c.nowMillis<c.ownerStartedMillis||c.uploadLimitBytesPerSecond<0
      ||c.downloadLimitBytesPerSecond<0||c.uploadLimitBytesPerSecond>Max||c.downloadLimitBytesPerSecond>Max||!absentOrNonnegative(c.uploadBytesPerSecond)
      ||!absentOrNonnegative(c.downloadBytesPerSecond)||!absentOrNonnegative(c.smoothedUploadBytesPerSecond)){
    fail(Code::InvalidFacts,"queue clock/rates");
  }
  std::set<std::uint64_t> tokens;
  for(const auto& f:facts){
    if(!f.token||!f.position||f.position>static_cast<std::uint32_t>(Max)||!tokens.insert(f.token).second||static_cast<unsigned>(f.state)>8){fail(Code::InvalidFacts,"queue identity/state");}
    if((f.shareRatio&&*f.shareRatio< -1)||(f.minimumShareRatio&&*f.minimumShareRatio<0)
        ||(f.maximumShareRatio&&*f.maximumShareRatio<0)||f.staleRankOffset<0
        ||(f.swarm&&(f.swarm->seeds<0||f.swarm->peers<0))||(f.connectedLeechers&&*f.connectedLeechers<0)){
      fail(Code::InvalidFacts,"queue ratio/swarm");
    }
    for(const auto& x:{f.secondsDownloading,f.secondsSeeding,f.secondsSinceLastUpload,f.runningMillis,f.seedingMillis,
        f.secondsSinceLastActive,f.downloadBytesPerSecond,f.uploadBytesPerSecond,f.smoothedUploadBytesPerSecond,
        f.diskReadLatencyMillis,f.diskWriteLatencyMillis,f.sizeBytes,f.remainingBytes,f.etaSeconds}){
      if(!absentOrNonnegative(x)){fail(Code::InvalidFacts,"queue observation");}
    }
    const auto& m=f.activity;
    for(const auto& time:{m.downloadChangeMillis,m.seedChangeMillis,m.downloadHighLatencyMillis,m.seedHighLatencyMillis,m.lastObservationMillis}){
      if(time&&(*time<0||*time>c.nowMillis)){fail(Code::InvalidFacts,"queue activity time");}
    }
    if(m.timedMinimumMillis<0){fail(Code::InvalidFacts,"queue timed minimum");}
    if(f.filePriority&&f.filePriority->size()!=4){fail(Code::InvalidFacts,"file priority stats");}
    if(f.remainingBytes&&f.sizeBytes&&*f.remainingBytes>*f.sizeBytes){fail(Code::InvalidFacts,"remaining bytes");}
    if(f.secondsDownloading&&f.secondsSeeding&&*f.secondsDownloading>std::numeric_limits<std::int64_t>::max()-*f.secondsSeeding){
      fail(Code::InvalidFacts,"elapsed total");
    }
  }
}
struct Swarm { std::int32_t seeds=0,peers=0;bool observed=false,valid=false,rankValid=false; };
Swarm swarm(const QueuePolicy& p,const QueueTorrentFacts& f) {
  if(!f.swarm){return {};}
  Swarm s{f.swarm->seeds,f.swarm->peers,true,f.swarm->valid,false};
  if(p.peersAsFullCopy>0&&s.seeds>=p.fakeFullCopySeedStart){
    const auto augmented=static_cast<std::int64_t>(s.seeds)+s.peers/p.peersAsFullCopy;
    if(augmented>Max){fail(Code::InvalidFacts,"fake full-copy seed count");}
    s.seeds=static_cast<std::int32_t>(augmented);
  }
  s.rankValid=s.valid||s.seeds>0||s.peers>0;
  return s;
}
struct Truth {bool value=false,known=true;};
Truth firstPriority(const QueuePolicy& p,const QueueTorrentFacts& f,const Swarm& s) {
  if(f.externallyFirstPriority){return {true,true};}
  if(f.externallyNotFirstPriority||!f.persistent||!f.complete||f.state==QueueTorrentState::Stopped||f.state==QueueTorrentState::Failed){return {};}
  if(p.firstPriorityIgnoreSeedPeerRatio>0&&s.observed&&s.peers>0&&s.seeds>0
      &&s.seeds/s.peers>=p.firstPriorityIgnoreSeedPeerRatio){return {};}
  if(p.firstPriorityIgnoreZeroPeers&&s.valid&&s.peers==0){return {};}
  if(p.firstPriorityIgnoreIdleMinutes>0){
    const auto idle=f.secondsSinceLastUpload?f.secondsSinceLastUpload:f.secondsSeeding;
    if(idle&&*idle>60LL*p.firstPriorityIgnoreIdleMinutes){return {};}
  }
  bool any=false,all=true,unknown=false;
  const auto add=[&](Truth t){if(!t.known){unknown=true;}else{any|=t.value;all&=t.value;}};
  const auto minimum=f.minimumShareRatio&&*f.minimumShareRatio>0?*f.minimumShareRatio:p.firstPriorityShareRatio;
  add(f.shareRatio?Truth{*f.shareRatio!=-1&&*f.shareRatio<minimum,true}:Truth{false,false});
  if(p.firstPrioritySeedMinutes>0){add(f.secondsSeeding?Truth{*f.secondsSeeding<60LL*p.firstPrioritySeedMinutes,true}:Truth{false,false});}
  if(p.firstPriorityActiveMinutes>0){
    if(!f.secondsSeeding||!f.secondsDownloading){add({false,false});}
    else {
      const auto limit=60LL*p.firstPriorityActiveMinutes;
      add({*f.secondsSeeding<limit&&*f.secondsDownloading<limit-*f.secondsSeeding,true});
    }
  }
  if(p.firstPriorityMode==QueueFirstPriorityMode::Any){return any?Truth{true,true}:Truth{false,!unknown};}
  return !all?Truth{false,true}:Truth{!unknown,!unknown};
}
std::int64_t minSeedMillis(const QueuePolicy& p,const QueueTorrentFacts& f,const QueueContext& c) {
  auto result=1000LL*p.minimumSeedSeconds;
  const auto difference=1000LL*(static_cast<std::int64_t>(p.minimumSeedActiveSeconds)-p.minimumSeedSeconds);
  if(difference>0){
    if(!f.smoothedUploadBytesPerSecond||!c.smoothedUploadBytesPerSecond){return result+difference;}
    if(*f.smoothedUploadBytesPerSecond>0&&*c.smoothedUploadBytesPerSecond>0){
      if(*f.smoothedUploadBytesPerSecond>=*c.smoothedUploadBytesPerSecond){result+=difference;}
      else {result+=static_cast<std::int64_t>(static_cast<double>(difference)
          *(static_cast<double>(*f.smoothedUploadBytesPerSecond)/static_cast<double>(*c.smoothedUploadBytesPerSecond)));}
    }
  }
  return result;
}
std::int64_t timedMinimum(const QueuePolicy& p,const QueueTorrentFacts& f,const QueueContext& c,const Swarm& s) {
  auto result=minSeedMillis(p,f,c);
  if(p.timedMinimumWithPeersSeconds>0&&f.connectedLeechers&&*f.connectedLeechers>0){
    result=1000LL*p.timedMinimumWithPeersSeconds;
    if(p.timedMaximumWithPeersSeconds>p.timedMinimumWithPeersSeconds&&s.observed&&s.peers>0){
      if(s.seeds==0){result=1000LL*p.timedMaximumWithPeersSeconds;}
      else {
        const float ratio=static_cast<float>(s.peers)/static_cast<float>(s.seeds);
        const auto diff=1000LL*(static_cast<std::int64_t>(p.timedMaximumWithPeersSeconds)-p.timedMinimumWithPeersSeconds);
        // Java compound long += double truncates the complete expression.
        result=static_cast<std::int64_t>(static_cast<double>(result)+static_cast<double>(diff)-static_cast<double>(diff)/(1.0+ratio));
      }
    }
    result=std::max<std::int64_t>(result,f.activity.timedMinimumMillis);
  }
  return result;
}
std::int32_t jint(std::uint32_t n){return std::bit_cast<std::int32_t>(n);}
std::int64_t jlong(std::uint64_t n){return std::bit_cast<std::int64_t>(n);}
std::int32_t rank(const QueuePolicy& p,const QueueTorrentFacts& f,const QueueContext&,const Swarm& s,
    const Truth fp,QueueReason& reason,std::int64_t timeMinimum) {
  if(!f.complete){return jint(1000000000U+(10000U-f.position));}
  if(!fp.value&&fp.known){
    const auto maxRatio=f.maximumShareRatio&&*f.maximumShareRatio>0?*f.maximumShareRatio:p.ignoreShareRatio;
    if(maxRatio>0&&f.shareRatio&&*f.shareRatio!=-1&&*f.shareRatio>=maxRatio
        &&((s.observed&&s.seeds>=p.ignoreShareRatioSeedStart)||!s.rankValid)){
      reason=QueueReason::IgnoreShareRatio;return -8;
    }
    if(p.ignoreZeroPeers&&s.rankValid&&s.peers==0){reason=QueueReason::IgnoreZeroPeers;return -7;}
    if(p.ignoreSeedCount>0&&s.observed&&s.seeds>=p.ignoreSeedCount){reason=QueueReason::IgnoreSeedCount;return -5;}
    if(p.ignorePeerSeedRatio>0&&s.observed&&s.seeds>0&&s.seeds>=p.ignorePeerSeedRatioSeedStart
        &&static_cast<float>(s.peers)/static_cast<float>(s.seeds)<=static_cast<float>(p.ignorePeerSeedRatio)){
      reason=QueueReason::IgnorePeerSeedRatio;return -4;
    }
  }
  if(p.rankMode==QueueRankMode::None){return 0;}
  if(p.rankMode==QueueRankMode::Timed){
    if(fp.value){return 200000000;}
    if(f.state==QueueTorrentState::Stopped||f.state==QueueTorrentState::Stopping||f.state==QueueTorrentState::Failed){return -2;}
    if(f.state!=QueueTorrentState::Queued){
      const auto elapsed=f.state==QueueTorrentState::Seeding&&!f.forceStart?f.seedingMillis.value_or(0):0;
      if(elapsed>=timeMinimum){reason=QueueReason::TimedRotation;return 1;}
      const auto seconds=static_cast<std::uint32_t>(static_cast<std::uint64_t>(elapsed/1000));
      return jint(200000000U+seconds);
    }
    if(f.secondsSinceLastActive){return static_cast<std::int32_t>(std::min<std::int64_t>(*f.secondsSinceLastActive,199999998));}
    auto seconds=f.secondsSeeding.value_or(0);
    if(seconds>199899999){seconds=199899999+static_cast<std::int64_t>(f.position);}
    return jint(199999999U-static_cast<std::uint32_t>(static_cast<std::uint64_t>(seconds)));
  }
  if(!s.rankValid){reason=QueueReason::UnknownFacts;return -9;}
  std::int32_t result=0;
  if(p.rankMode==QueueRankMode::PeerCount){
    if(s.peers>jint(static_cast<std::uint32_t>(s.seeds)*10U)){
      result=jint(100U*static_cast<std::uint32_t>(s.peers)*10U);
    }else{
      const auto numerator=jlong(100ULL*static_cast<std::uint64_t>(s.peers)*static_cast<std::uint64_t>(s.peers));
      const auto denominator=jint(static_cast<std::uint32_t>(s.seeds)+1U);
      result=jint(static_cast<std::uint32_t>(static_cast<std::uint64_t>(numerator/denominator)));
    }
  }else if(p.rankMode==QueueRankMode::SeedCount&&(p.seedCountFallback==0||p.seedCountFallback>s.seeds)){
    result=(s.seeds<10000?10000-s.seeds:1)*100000;
  }else if(s.peers!=0){
    if(s.seeds==0){result=s.peers>=p.minimumPeersToBoostNoSeeds?99999:0;}
    else {
      const float x=static_cast<float>(s.seeds)/static_cast<float>(s.peers);
      const float plus=x+1.0F;
      result=static_cast<std::int32_t>(99999.0F/(plus*plus));
    }
  }
  if(f.staleRankOffset>0&&result>f.staleRankOffset){result-=f.staleRankOffset;}
  return result<0?1:result;
}
struct Activity {bool value=false,known=true;};
Activity active(const QueuePolicy& p,const QueueTorrentFacts& f,const QueueContext& c,const Swarm& s,
    const Truth fp,QueueActivityMemory& m,bool seed) {
  const auto state=seed?QueueTorrentState::Seeding:QueueTorrentState::Downloading;
  auto& previous=seed?m.seeding:m.downloading;
  auto& change=seed?m.seedChangeMillis:m.downloadChangeMillis;
  auto& high=seed?m.seedHighLatencyMillis:m.downloadHighLatencyMillis;
  if(f.state!=state){previous=false;change.reset();return {};}
  if(seed&&p.autoStartZeroPeers&&s.observed&&s.peers==0){previous=false;change.reset();return {};}
  if(seed&&p.rankMode==QueueRankMode::Timed&&!fp.value){previous=true;change.reset();return {true,fp.known};}
  if(f.runningMillis&&*f.runningMillis<=30000){previous=true;change.reset();return {true,true};}
  const auto rate=seed?f.uploadBytesPerSecond:f.downloadBytesPerSecond;
  if(!rate){return {previous,false};}
  bool candidate=*rate>=(seed?p.minimumSeedBytesPerSecond:p.minimumDownloadBytesPerSecond);
  const auto latency=seed?f.diskReadLatencyMillis:f.diskWriteLatencyMillis;
  if(latency&&*latency>2500){high=c.nowMillis;candidate=true;}
  else if(high){if(c.nowMillis-*high<=60000){candidate=true;}else{high.reset();}}
  if(candidate!=previous){
    if(!change){change=c.nowMillis;}
    if(c.nowMillis-*change>=10000){previous=candidate;}
  }else{change.reset();}
  return {previous,true};
}
bool stopped(const QueueTorrentFacts& f){return f.state==QueueTorrentState::Stopped||f.state==QueueTorrentState::Stopping||f.state==QueueTorrentState::Failed;}
bool waiting(const QueueTorrentFacts& f){return f.state==QueueTorrentState::Waiting||f.state==QueueTorrentState::Preparing||f.state==QueueTorrentState::Checking;}
bool running(const QueueTorrentFacts& f){return f.state==QueueTorrentState::Downloading||f.state==QueueTorrentState::Seeding;}
bool ignored(QueueReason r){return r==QueueReason::IgnoreShareRatio||r==QueueReason::IgnoreZeroPeers||r==QueueReason::IgnoreSeedCount||r==QueueReason::IgnorePeerSeedRatio;}
} // namespace

std::expected<QueuePolicy,QueuePolicyError> readQueuePolicy(const Document* document) {
  try{
    if(document&&(document->root().type!=Node::Type::Dictionary||document->root().children.size()%2!=0)){
      fail(Code::InvalidConfiguration,"configuration root");
    }
    if(document&&(document->originalBytes().size()>Limits{}.inputBytes||document->root().children.size()>Limits{}.nodes)){
      fail(Code::Limit,"queue configuration");
    }
    const Reader r{document};QueuePolicy p;
#define INT(field,key) p.field=r.integer(key,p.field)
#define BOOL(field,key) p.field=r.boolean(key,p.field)
    INT(maximumDownloads,"max downloads");INT(maximumActive,"max active torrents");INT(minimumDownloads,"min downloads");
    BOOL(linkMinimumDownloads,"StartStopManager_bMaxMinDLLinked");BOOL(ignoreChecking,"StartStopManager_bMaxDownloadIgnoreChecking");
    BOOL(maximumActiveWhenSeedingEnabled,"StartStopManager_bMaxActiveTorrentsWhenSeedingEnabled");
    INT(maximumActiveWhenSeeding,"StartStopManager_iMaxActiveTorrentsWhenSeeding");
    p.rankMode=static_cast<QueueRankMode>(r.integer("StartStopManager_iRankType",1,4));
    p.downloadOrder=static_cast<QueueDownloadOrder>(r.integer("StartStopManager_Downloading_iSortType",0,8));
    BOOL(preferLargerSwarms,"StartStopManager_bPreferLargerSwarms");
    INT(seedCountFallback,"StartStopManager_iRankTypeSeedFallback");INT(minimumPeersToBoostNoSeeds,"StartStopManager_iMinPeersToBoostNoSeeds");
    INT(minimumDownloadBytesPerSecond,"StartStopManager_iMinSpeedForActiveDL");INT(minimumSeedBytesPerSecond,"StartStopManager_iMinSpeedForActiveSeeding");
    p.minimumSeedSeconds=r.integer("StartStopManager_iMinSeedingTime",300,MaxSeconds);
    p.minimumSeedActiveSeconds=r.integer("StartStopManager_iMinSeedingTimeWhenActive",900,MaxSeconds);
    p.timedMinimumWithPeersSeconds=r.integer("StartStopManager_iTimed_MinSeedingTimeWithPeers",0,MaxSeconds);
    p.timedMaximumWithPeersSeconds=r.integer("StartStopManager_iTimed_MaxSeedingTimeWithPeers",0,MaxSeconds);
    INT(maximumStalledSeeds,"StartStopManager_iMaxStalledSeeding");BOOL(stalledSeedsIgnoreZeroPeers,"StartStopManager_bMaxStalledSeedingIgnoreZP");
    BOOL(autoStartZeroPeers,"StartStopManager_bAutoStart0Peers");
    p.firstPriorityMode=static_cast<QueueFirstPriorityMode>(r.integer("StartStopManager_iFirstPriority_Type",1,1));
    INT(firstPriorityShareRatio,"StartStopManager_iFirstPriority_ShareRatio");
    p.firstPrioritySeedMinutes=r.integer("StartStopManager_iFirstPriority_SeedingMinutes",0,Max/60);
    p.firstPriorityActiveMinutes=r.integer("StartStopManager_iFirstPriority_DLMinutes",0,Max/60);
    INT(firstPriorityIgnoreSeedPeerRatio,"StartStopManager_iFirstPriority_ignoreSPRatio");
    INT(firstPriorityIgnoreIdleMinutes,"StartStopManager_iFirstPriority_ignoreIdleMinutes");
    bool oldUi=false;
    if(const auto* ui=r.find("ui")){if(ui->type!=Node::Type::Bytes){fail(Code::InvalidConfiguration,"ui");}oldUi=ui->bytes=="az2";}
    p.firstPriorityIgnoreZeroPeers=r.boolean("StartStopManager_bFirstPriority_ignore0Peer",!oldUi);
    p.ignoreShareRatio=r.stopRatio();INT(ignoreShareRatioSeedStart,"StartStopManager_iIgnoreShareRatioSeedStart");
    INT(ignoreSeedCount,"StartStopManager_iIgnoreSeedCount");INT(ignorePeerSeedRatio,"Stop Peers Ratio");
    INT(ignorePeerSeedRatioSeedStart,"StartStopManager_iIgnoreRatioPeersSeedStart");BOOL(ignoreZeroPeers,"StartStopManager_bIgnore0Peers");
    INT(peersAsFullCopy,"StartStopManager_iNumPeersAsFullCopy");INT(fakeFullCopySeedStart,"StartStopManager_iFakeFullCopySeedStart");
    BOOL(stopOnceBandwidthMet,"StartStopManager_bStopOnceBandwidthMet");
    BOOL(preventNewSeedsAtUploadLimit,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMet");
    INT(uploadLimitSlack,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMetSlack");
    BOOL(uploadLimitSlackIsPercent,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMetPercent");
    BOOL(newSeedsMoveTop,"StartStopManager_bNewSeedsMoveTop");BOOL(retainForceStartOnComplete,"StartStopManager_bRetainForceStartWhenComplete");
    BOOL(lightSeedingEnabled,"Enable Light Seeding");INT(reservedLightSeedSlots,"Light Seed Slots Reserved");INT(flexibleSeedSlots,"Flexible Seed Slots");
#undef INT
#undef BOOL
    validatePolicy(p);return p;
  }catch(const Invalid& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{Code::Memory,"queue policy"});}
}

std::expected<QueuePlan,QueuePolicyError> evaluateQueue(const QueuePolicy& p,
    std::span<const QueueTorrentFacts> facts,const QueueContext& c) {
  try{
    validatePolicy(p);validateFacts(facts,c);QueuePlan plan;plan.decisions.resize(facts.size());
    std::vector<Swarm> swarms; swarms.reserve(facts.size());
    std::vector<std::int64_t> minimums;minimums.reserve(facts.size());
    std::vector<bool> fixed(facts.size(),false);
    std::vector<std::size_t> order(facts.size());std::iota(order.begin(),order.end(),std::size_t{0});
    if((p.lightSeedingEnabled&&p.reservedLightSeedSlots>0)||p.flexibleSeedSlots>0){plan.unavailableInputs|=4;}
    if(p.downloadOrder==QueueDownloadOrder::Speed||p.downloadOrder==QueueDownloadOrder::Eta){plan.unavailableInputs|=16;}
    bool seedsMayStart=p.rankMode==QueueRankMode::None||p.rankMode==QueueRankMode::Timed||c.nowMillis-c.ownerStartedMillis>90000;
    bool allScraped=true;
    std::int64_t forceSeeds=0,forceSeedsNonFP=0,firstSeeds=0,heldSeeds=0,preparedDownloads=0;
    for(std::size_t i=0;i<facts.size();++i){
      const auto& f=facts[i];auto& d=plan.decisions[i];const auto s=swarm(p,f);swarms.push_back(s);
      if(!s.observed){plan.unavailableInputs|=1;}
      if(!f.diskReadLatencyMillis||!f.diskWriteLatencyMillis){plan.unavailableInputs|=8;}
      const auto fp=firstPriority(p,f,s);d.token=f.token;d.firstPriority=fp.value;d.firstPriorityKnown=fp.known;
      d.activity=f.activity;const auto dl=active(p,f,c,s,fp,d.activity,false),seed=active(p,f,c,s,fp,d.activity,true);
      d.activity.lastObservationMillis=c.nowMillis;d.activelyDownloading=dl.value;d.activelySeeding=seed.value;
      d.activityKnown=dl.known&&seed.known;
      if(p.stopOnceBandwidthMet){
        const auto reached=[](const std::optional<std::int64_t>& rate,std::int64_t limit){
          return rate&&limit>0&&static_cast<double>(*rate)/1024.0>static_cast<double>(limit/1024)*static_cast<double>(0.9F);
        };
        const bool downReached=reached(c.downloadBytesPerSecond,c.downloadLimitBytesPerSecond);
        const bool upReached=reached(c.uploadBytesPerSecond,c.uploadLimitBytesPerSecond);
        if(f.state==QueueTorrentState::Downloading&&downReached){d.activelyDownloading=true;}
        if(f.state==QueueTorrentState::Seeding&&(upReached||downReached)){d.activelySeeding=true;}
      }
      if(!fp.known||!d.activityKnown||(f.complete&&p.minimumSeedActiveSeconds>p.minimumSeedSeconds
          &&(!f.smoothedUploadBytesPerSecond||!c.smoothedUploadBytesPerSecond))
          ||(f.state==QueueTorrentState::Seeding&&!f.runningMillis)){plan.unavailableInputs|=2;}
      const auto minimum=p.rankMode==QueueRankMode::Timed?timedMinimum(p,f,c,s):minSeedMillis(p,f,c);
      minimums.push_back(minimum);
      if(p.rankMode==QueueRankMode::Timed&&p.timedMinimumWithPeersSeconds>0&&f.connectedLeechers&&*f.connectedLeechers>0){
        d.activity.timedMinimumMillis=minimum;
      }
      d.seedingRank=rank(p,f,c,s,fp,d.reason,minimum);
      d.clearForceStart=f.newlyCompleted&&f.forceStart&&!p.retainForceStartOnComplete;
      d.moveNewSeedToTop=f.newlyCompleted&&p.newSeedsMoveTop;
      if(f.manuallyPaused){d.reason=QueueReason::ManualPause;fixed[i]=true;continue;}
      if(stopped(f)){d.reason=QueueReason::Stopped;fixed[i]=true;continue;}
      if(!f.controllable||f.moving){
        d.reason=QueueReason::Uncontrollable;fixed[i]=true;
        if(running(f)||waiting(f)){if(f.complete){++heldSeeds;}else{++preparedDownloads;}}
        continue;
      }
      if(f.complete){
        allScraped&=s.valid;
        if((s.valid&&s.seeds==0)||fp.value||(d.seedingRank>0&&c.nowMillis-c.ownerStartedMillis>20000)){seedsMayStart=true;}
        if(fp.value&&!(f.state==QueueTorrentState::Seeding&&d.activityKnown&&!d.activelySeeding)){++firstSeeds;}
      }
      if(f.forceStart&&!d.clearForceStart){
        fixed[i]=true;d.action=QueueAction::Run;d.reason=QueueReason::Forced;
        if(f.complete&&(d.activelySeeding||!d.activityKnown)){++forceSeeds;if(!fp.value){++forceSeedsNonFP;}}
        continue;
      }
      if(waiting(f)){
        fixed[i]=true;d.reason=f.state==QueueTorrentState::Checking?QueueReason::Checking:QueueReason::Preparing;
        if(!f.complete&&!(p.ignoreChecking&&f.state==QueueTorrentState::Checking)){++preparedDownloads;}
        if(f.complete){++heldSeeds;}
        continue;
      }
      if(f.complete&&f.state==QueueTorrentState::Seeding&&(!f.runningMillis||*f.runningMillis<minimum)){
        fixed[i]=true;++heldSeeds;d.reason=QueueReason::MinimumSeedingTime;
      }
    }
    seedsMayStart|=allScraped;
    const auto positionLess=[&](std::size_t a,std::size_t b){return facts[a].position!=facts[b].position?facts[a].position<facts[b].position:facts[a].token<facts[b].token;};
    const auto optionalCompare=[](const auto& a,const auto& b,bool descending)->int{
      if(a.has_value()!=b.has_value()){return a?-1:1;}
      if(!a||*a==*b){return 0;}return ((*a<*b)!=descending)?-1:1;
    };
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
      const auto& x=facts[a];const auto& y=facts[b];const auto& dx=plan.decisions[a];const auto& dy=plan.decisions[b];
      if(dx.firstPriority!=dy.firstPriority){return dx.firstPriority;}
      if(x.complete!=y.complete){return !x.complete;}
      if(!x.complete){
        if(c.nowMillis-c.ownerStartedMillis>=30000){
          int compare=0;
          switch(p.downloadOrder){
            case QueueDownloadOrder::Seeds:case QueueDownloadOrder::ReverseSeeds:
              if(swarms[a].observed&&swarms[b].observed){
                const auto sx=swarms[a].seeds,sy=swarms[b].seeds;
                const auto px=swarms[a].peers,py=swarms[b].peers;
                if(sx!=sy){compare=sx>sy?-1:1;}else if(px!=py){compare=px>py?-1:1;}
                if(p.downloadOrder==QueueDownloadOrder::ReverseSeeds){compare=-compare;}
              }else{plan.unavailableInputs|=1;if(swarms[a].observed!=swarms[b].observed){compare=swarms[a].observed?-1:1;}}
              break;
            case QueueDownloadOrder::Size:compare=optionalCompare(x.sizeBytes,y.sizeBytes,true);break;
            case QueueDownloadOrder::ReverseSize:compare=optionalCompare(x.sizeBytes,y.sizeBytes,false);break;
            case QueueDownloadOrder::Remaining:compare=optionalCompare(x.remainingBytes,y.remainingBytes,false);break;
            case QueueDownloadOrder::FilePriority:
              if(x.filePriority&&y.filePriority){
                for(const auto n:{1U,3U}){if((*x.filePriority)[n]!=(*y.filePriority)[n]){compare=(*x.filePriority)[n]>(*y.filePriority)[n]?-1:1;break;}}
              }else{plan.unavailableInputs|=2;if(x.filePriority.has_value()!=y.filePriority.has_value()){compare=x.filePriority?-1:1;}}
              break;
            default:break; // Dynamic original speed/ETA probing is unavailable.
          }
          if(compare){return compare<0;}
        }
        return positionLess(a,b);
      }
      if(p.rankMode!=QueueRankMode::None){
        if(dx.seedingRank!=dy.seedingRank){return dx.seedingRank>dy.seedingRank;}
        if(p.rankMode!=QueueRankMode::Timed){
          if(swarms[a].observed!=swarms[b].observed){return swarms[a].observed;}
          if(swarms[a].observed&&swarms[a].peers!=swarms[b].peers){
            return p.preferLargerSwarms?swarms[a].peers>swarms[b].peers:swarms[a].peers<swarms[b].peers;
          }
          const auto compare=optionalCompare(x.shareRatio,y.shareRatio,false);if(compare){return compare<0;}
        }
      }
      return positionLess(a,b);
    });
    std::int64_t maximumActive=p.maximumActive;
    if(p.maximumActiveWhenSeedingEnabled&&c.seedingOnly){
      bool incompleteMayStart=false;
      for(const auto& f:facts){if(!f.complete&&!stopped(f)&&!running(f)){incompleteMayStart=true;}}
      if(p.maximumActiveWhenSeeding<=p.maximumActive||!incompleteMayStart){maximumActive=p.maximumActiveWhenSeeding;}
    }
    const auto infinity=static_cast<std::int64_t>(MaximumQueueTorrents)*4;
    const auto minDownloads=p.linkMinimumDownloads?p.maximumDownloads:p.minimumDownloads;
    auto dlLimit=p.maximumDownloads==0?infinity:static_cast<std::int64_t>(p.maximumDownloads);
    if(maximumActive>0&&p.maximumDownloads>0){dlLimit=std::min(dlLimit,std::max<std::int64_t>(0,maximumActive-firstSeeds-forceSeedsNonFP));}
    dlLimit=std::max(dlLimit,static_cast<std::int64_t>(minDownloads));
    auto downloads=preparedDownloads;
    for(const auto i:order){
      const auto& f=facts[i];auto& d=plan.decisions[i];if(f.complete||fixed[i]){continue;}
      if(f.state==QueueTorrentState::Downloading&&d.activityKnown&&!d.activelyDownloading){
        d.action=QueueAction::Preserve;continue; // Stalled download consumes no active slot.
      }
      if(downloads<dlLimit){d.action=QueueAction::Run;d.reason=QueueReason::Eligible;++downloads;}
      else{d.action=QueueAction::Queue;d.reason=QueueReason::DownloadLimit;}
    }
    auto seedLimit=maximumActive==0?infinity:std::max<std::int64_t>(0,maximumActive-downloads);
    auto seeds=forceSeeds+heldSeeds;std::int64_t stalled=0;
    const auto maxStalled=p.minimumSeedBytesPerSecond==0?0:(p.maximumStalledSeeds==0?999:p.maximumStalledSeeds);
    bool uploadProhibits=false;
    if(p.preventNewSeedsAtUploadLimit&&c.uploadLimitBytesPerSecond>0){
      if(c.smoothedUploadBytesPerSecond){
        const auto limitKiB=c.uploadLimitBytesPerSecond/1024;
        const auto target=p.uploadLimitSlackIsPercent?limitKiB*p.uploadLimitSlack/100:limitKiB-p.uploadLimitSlack;
        uploadProhibits=*c.smoothedUploadBytesPerSecond/1024>target;
      }else{plan.unavailableInputs|=2;}
    }
    for(const auto i:order){
      const auto& f=facts[i];auto& d=plan.decisions[i];const auto& s=swarms[i];
      if(!f.complete||fixed[i]){continue;}
      if(!seedsMayStart){d.action=QueueAction::Preserve;d.reason=QueueReason::WaitingForScrape;continue;}
      if(p.autoStartZeroPeers&&s.valid&&s.peers==0){d.action=QueueAction::Run;d.reason=QueueReason::Eligible;continue;}
      if((ignored(d.reason)&&d.firstPriorityKnown)||d.seedingRank < -1){d.action=QueueAction::Queue;continue;}
      if(!d.firstPriorityKnown){d.action=QueueAction::Preserve;d.reason=QueueReason::UnknownFacts;continue;}
      const bool timedExpired=p.rankMode==QueueRankMode::Timed&&!d.firstPriority
          &&f.state==QueueTorrentState::Seeding&&f.seedingMillis&&*f.seedingMillis>=minimums[i];
      bool alternative=false;
      if(timedExpired){for(const auto j:order){if(j!=i&&facts[j].complete&&!fixed[j]&&facts[j].state==QueueTorrentState::Queued&&!ignored(plan.decisions[j].reason)){alternative=true;break;}}}
      if(timedExpired&&alternative){d.action=QueueAction::Queue;d.reason=QueueReason::TimedRotation;continue;}
      if(f.state==QueueTorrentState::Seeding&&d.activityKnown&&!d.activelySeeding){
        const bool exempt=p.stalledSeedsIgnoreZeroPeers&&s.valid&&s.peers==0;
        if(exempt||stalled<maxStalled){d.action=QueueAction::Preserve;if(!exempt){++stalled;}continue;}
        d.action=QueueAction::Queue;d.reason=QueueReason::StalledLimit;continue;
      }
      if(uploadProhibits&&f.state==QueueTorrentState::Queued){d.action=QueueAction::Queue;d.reason=QueueReason::UploadLimit;continue;}
      if(seeds<seedLimit){d.action=QueueAction::Run;if(d.reason!=QueueReason::UnknownFacts){d.reason=d.firstPriority?QueueReason::FirstPriority:QueueReason::Eligible;}++seeds;}
      else{d.action=QueueAction::Queue;d.reason=QueueReason::ActiveLimit;}
    }
    return plan;
  }catch(const Invalid& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{Code::Memory,"queue plan"});}
}
} // namespace biglybtc::core
