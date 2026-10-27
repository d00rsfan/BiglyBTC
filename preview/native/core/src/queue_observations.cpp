#include "biglybtc/queue_observations.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
namespace biglybtc::core {
std::int64_t sourceQueueSum(std::int64_t a,std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(a)+static_cast<std::uint64_t>(b));
}
std::int32_t sourceQueueShareRatio(std::int64_t downloaded,std::int64_t uploaded,
    std::int64_t discarded,std::int64_t failed) noexcept {
  auto good=std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(downloaded)
      -static_cast<std::uint64_t>(sourceQueueSum(discarded,failed)));
  if(good<=0){return -1;}
  const auto product=std::bit_cast<std::int64_t>(std::uint64_t{1000}*static_cast<std::uint64_t>(uploaded));
  const auto ratio=product/good;
  if(ratio>std::numeric_limits<std::int32_t>::max()){return std::numeric_limits<std::int32_t>::max();}
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(ratio));
}
bool QueueOwnerObservations::trackerOrder(std::span<const std::array<unsigned char,32>> keys) noexcept {
  if(sequence_||count_||keys.size()>MaximumTrackers){return false;}
  for(const auto& key:keys){bool duplicate=false;for(unsigned i=0;i<count_;++i){duplicate|=scrapes_[i].key==key;}
    if(!duplicate){scrapes_[count_++].key=key;}}
  orderKnown_=true;return true;
}
bool QueueOwnerObservations::restoreAggregate(std::string_view raw) noexcept {
  if(sequence_||aggregate_||lost_){return false;}
  const auto comma=raw.find(',');if(comma==raw.npos){return false;}
  const auto next=raw.find(',',comma+1);if(next==raw.npos||raw.find(',',next+1)!=raw.npos){return false;}
  std::int32_t minutes=0,seeds=0,peers=0;
  const auto read=[](std::string_view s,auto& value){if(!s.empty()&&s.front()=='+'){s.remove_prefix(1);}const auto r=std::from_chars(s.data(),s.data()+s.size(),value);return !s.empty()&&r.ec==std::errc{}&&r.ptr==s.data()+s.size();};
  if(!read(raw.substr(0,comma),minutes)||!read(raw.substr(comma+1,next-comma-1),seeds)
      ||!read(raw.substr(next+1),peers)){return false;}
  Scrape value;value.seeds=seeds;value.peers=peers;value.good=true;
  const auto seconds=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(minutes)*std::uint32_t{60});
  value.started=seconds>0?static_cast<std::int64_t>(seconds)*1000:-1;
  aggregate_=value;aggregateCached_=true;everSuccess_=value.started>0||seeds>0||peers>0;return true;
}
void QueueOwnerObservations::restoreLegacy(std::int64_t packed) noexcept {
  if(sequence_||aggregate_||lost_||packed==-1){return;}
  Scrape value;const auto raw=static_cast<std::uint64_t>(packed);
  value.seeds=static_cast<std::int32_t>((raw>>32)&0x00ffffff);value.peers=static_cast<std::int32_t>(raw&0x00ffffff);
  value.started=-1;value.good=true;aggregate_=value;aggregateCached_=true;everSuccess_=value.seeds>0||value.peers>0;
}
QueueObservedFacts QueueOwnerObservations::facts(std::int64_t now) const noexcept {
  QueueObservedFacts result;if(lost_||protocol_<1||protocol_>2||now<lastMillis_){return result;}
  auto down=downloadBase_,seed=seedBase_;
  if(started_){
    const auto end=seedStarted_.value_or(now);
    if(end>*started_){down=sourceQueueSum(down,(end-*started_)/1000);}
    if(seedStarted_){seed=sourceQueueSum(seed,(now-*seedStarted_)/1000);}
    result.runningMillis=now-*started_;result.seedingMillis=seedStarted_?now-*seedStarted_:0;
  }
  result.durations=QueueObservedDurations{down,seed};
  auto seeds=0,peers=0;
  if(aggregate_){
    if(aggregate_->started>0){
      seeds=aggregate_->seeds;peers=aggregate_->peers;
      if(started_){
        if(seeds>0&&seedStartedWall_&&*seedStartedWall_>0&&aggregate_->started>*seedStartedWall_){--seeds;}
        if(peers>0&&!seedStarted_&&startedWall_&&aggregate_->started>*startedWall_){--peers;}
      }
    }
  }
  if(!seeds&&announceSuccess_){seeds=announceSeeds_;}
  if(!peers&&announceSuccess_){peers=announcePeers_;}
  result.swarm=QueueObservedSwarm{std::max(seeds,0),std::max(peers,0),everSuccess_,true};
  return result;
}
std::optional<QueueObservedDurations> QueueOwnerObservations::settledDurations() const noexcept {
  if(lost_||started_){return {};}
  return QueueObservedDurations{downloadBase_,seedBase_};
}
bool QueueOwnerObservations::observe(const QueueObservation& event) noexcept {
  const auto kind=event.kind;const auto raw=static_cast<unsigned>(kind);
  if(lost_){return false;}
  if(!event.sequence||event.sequence<=sequence_||event.observedNanos<0||raw<1||raw>10
      ||event.observedNanos/1000000<lastMillis_||kind==QueueObservationKind::Lost){lost_=true;return false;}
  const auto now=event.observedNanos/1000000;
  if(raw>=6&&raw<=9){
    if(event.protocol!=protocol_){return true;} // Other immutable swarm is not authority.
    if(!event.request||event.requestStartedNanos<=0||event.requestStartedNanos>event.observedNanos
        ||event.seeds< -1||event.peers< -1){lost_=true;return false;}
  }else if(event.protocol||event.request){lost_=true;return false;}
  sequence_=event.sequence;lastMillis_=now;
  if(kind==QueueObservationKind::StartedDownload||kind==QueueObservationKind::StartedSeed){
    if(started_){lost_=true;return false;}
    started_=now;startedWall_=event.observedWallMillis;
    if(kind==QueueObservationKind::StartedSeed){seedStarted_=now;seedStartedWall_=event.observedWallMillis;}
    else{seedStarted_.reset();seedStartedWall_.reset();}
  }else if(kind==QueueObservationKind::Finished){if(started_&&!seedStarted_){seedStarted_=now;seedStartedWall_=event.observedWallMillis;}}
  else if(kind==QueueObservationKind::Unfinished){seedStarted_.reset();seedStartedWall_.reset();}
  else if(kind==QueueObservationKind::Stopped){
    const auto current=facts(now);
    if(!current.durations){lost_=true;return false;}
    downloadBase_=current.durations->downloading;seedBase_=current.durations->seeding;
    started_.reset();seedStarted_.reset();startedWall_.reset();seedStartedWall_.reset();
  }else if(kind==QueueObservationKind::Announce){
    announceSuccess_=true;announceSeeds_=event.seeds;announcePeers_=event.peers;
  }else if(kind==QueueObservationKind::AnnounceFailed){announceSuccess_=false;}
  else if(kind==QueueObservationKind::Scrape||kind==QueueObservationKind::ScrapeFailed){
    unsigned index=0;for(;index<count_&&scrapes_[index].key!=event.tracker;++index){}
    if(index==count_){
      if(orderKnown_){return true;} // Not an original configured scrape source.
      // Without authenticated original order, equal-count ties cannot safely
      // acquire arrival-order authority. Do not guess an alternative order.
      lost_=true;return false;
    }
    auto& value=scrapes_[index];
    if(kind==QueueObservationKind::Scrape){
      value.seeds=event.seeds;value.peers=event.peers;value.started=event.requestStartedWallMillis;value.good=true;everSuccess_=true;
    }else{value.good=false;}
    const Scrape* best=nullptr;
    for(unsigned i=0;i<count_;++i){const auto& candidate=scrapes_[i];if(candidate.good&&candidate.peers>=0
        &&(!best||candidate.peers>best->peers||(candidate.peers==best->peers&&candidate.seeds>best->seeds))){best=&candidate;}}
    if(best){aggregate_=*best;aggregateCached_=false;}
    else if(aggregate_&&aggregate_->key==value.key){aggregate_->good=false;}
  }
  return true;
}
}
