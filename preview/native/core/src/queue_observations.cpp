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
QueueOriginalScrapeResponse sourceQueueOriginalScrapeResponse(
    const std::optional<QueueOriginalScrapeContent>& content) noexcept {
  const bool valid=content&&(content->seeds!=-1||content->peers!=-1);
  return {valid?QueueOriginalScrapeType::Success:QueueOriginalScrapeType::Error,content};
}
bool sourceQueueQueuedScrapeEligible(const QueueOriginalScrapeResponse& response,
    const std::optional<std::int64_t>& ownStartedWallMillis) noexcept {
  if(response.type==QueueOriginalScrapeType::Error){return true;}
  return response.type==QueueOriginalScrapeType::Success&&response.content&&ownStartedWallMillis
      &&response.content->startedWallMillis==*ownStartedWallMillis;
}
namespace {
std::int32_t trackerInt(std::int64_t value) noexcept {return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
}
QueueOriginalScrapeResponse QueueOriginalTrackerResponse::response()const noexcept {
  if(!known_){return {};}
  return sourceQueueOriginalScrapeResponse(visible_?std::optional{current_}:std::nullopt);
}
bool QueueOriginalTrackerResponse::eligible(std::int64_t now)const noexcept {
  return known_&&regular_&&!pending_&&now>=0&&now<nextRegular_
      &&sourceQueueQueuedScrapeEligible(response(),ownStart_);
}
bool QueueOriginalTrackerResponse::regularDue(std::int64_t now)const noexcept {
  if(pending_||now<0){return false;}
  const auto current=response();
  // Once a real regular/announce response is valid and is not our own backup,
  // errors retain that validity. No further scrape is needed for this gate.
  if(known_&&current.type==QueueOriginalScrapeType::Success
      &&!sourceQueueQueuedScrapeEligible(current,ownStart_)){return false;}
  return !regular_||now>=nextRegular_;
}
bool QueueOriginalTrackerResponse::observeChronological(const QueueTrackerTerminal& value,std::int64_t stamp) noexcept {
  if(stamp<=0||value.observedMillis<0||(value.present&~7u)){unavailable();return false;}
  const bool accepted=stamp>lastChronologicalNanos_;
  if(accepted){lastChronologicalNanos_=stamp;observe(value);}
  const bool scrape=value.kind==QueueTrackerTerminalKind::Scrape||value.kind==QueueTrackerTerminalKind::ScrapeFailed;
  if(scrape&&stamp>lastChronologicalScrapeNanos_){
    // A real scrape assigns next time, even when a later announce's counts
    // already won. Reapply only announces occurring after that scrape.
    lastChronologicalScrapeNanos_=stamp;const auto delay=value.regularDelayMillis.value_or(600000);
    nextRegular_=delay>0&&value.observedMillis>INT64_MAX-delay?INT64_MAX:value.observedMillis+delay;regular_=true;
    for(std::size_t i=0;i<pendingAnnounceCount_;++i){const auto& contribution=pendingAnnounces_[i];
      if(contribution.observedNanos>stamp){nextRegular_=std::max(nextRegular_,contribution.deadlineMillis);}}
  }
  return accepted;
}
bool QueueOriginalTrackerResponse::announceTimingCurrent(std::int64_t stamp)const noexcept {
  return stamp>0&&stamp>lastChronologicalScrapeNanos_;
}
bool QueueOriginalTrackerResponse::deferRegularUntil(std::int64_t deadline,std::int64_t stamp) noexcept {
  if(stamp>0&&pendingChronologicalRequest_){
    if(pendingAnnounceCount_==MaximumPendingAnnounces){unavailable();return false;}
    pendingAnnounces_[pendingAnnounceCount_++]={stamp,deadline};
  }
  nextRegular_=regular_?std::max(nextRegular_,deadline):std::max<std::int64_t>(0,deadline);regular_=true;return true;
}
bool QueueOriginalTrackerResponse::regularPollDue(std::int64_t now)const noexcept {
  return !pending_&&now>=0&&(!regular_||now>=nextRegular_);
}
QueueTrackerSelectionStatus QueueOriginalTrackerResponse::selectionStatus()const noexcept {
  if(!known_){return QueueTrackerSelectionStatus::Unknown;}
  if(pending_){return QueueTrackerSelectionStatus::Scraping;}
  if(!visible_){return QueueTrackerSelectionStatus::Initializing;}
  return error_?QueueTrackerSelectionStatus::Error:QueueTrackerSelectionStatus::Online;
}
std::vector<std::size_t> sourceQueueTrackerOrder(std::span<const unsigned> tiers,std::int64_t seed){
  if(tiers.size()>8||!std::ranges::is_sorted(tiers)){return {};}
  constexpr std::uint64_t mask=(std::uint64_t{1}<<48)-1;
  auto state=(static_cast<std::uint64_t>(seed)^UINT64_C(0x5deece66d))&mask;
  const auto next=[&](unsigned bits){state=(state*UINT64_C(0x5deece66d)+11)&mask;return state>>(48-bits);};
  std::vector<std::size_t> order;order.reserve(tiers.size());std::size_t begin=0;
  for(std::size_t i=0;i<tiers.size();++i){
    if(i&&tiers[i]!=tiers[i-1]){begin=order.size();}
    const auto high=next(26);const auto low=next(27);
    const auto fraction=static_cast<double>((high<<27)+low)*0x1.0p-53;
    const auto position=static_cast<std::size_t>(fraction*static_cast<double>(order.size()-begin+1));
    order.insert(order.begin()+static_cast<std::ptrdiff_t>(begin+position),i);
  }
  return order;
}
std::optional<std::size_t> sourceQueueTrackerSelection(
    std::span<const QueueOriginalTrackerResponse* const> responses) noexcept {
  std::optional<std::size_t> backup,firstError;
  for(std::size_t i=0;i<responses.size();++i){
    if(!responses[i]){return {};}
    switch(responses[i]->selectionStatus()){
      case QueueTrackerSelectionStatus::Unknown:return {};
      case QueueTrackerSelectionStatus::Initializing:
      case QueueTrackerSelectionStatus::Scraping:return i;
      case QueueTrackerSelectionStatus::Online:
        if(!responses[i]->dhtBackup()){return i;}
        backup=i;break;
      case QueueTrackerSelectionStatus::Error:if(!firstError){firstError=i;}break;
    }
  }
  return backup?backup:firstError;
}
void QueueOriginalTrackerResponse::observe(const QueueTrackerTerminal& value) noexcept {
  if(value.observedMillis<0||(value.present&~7u)){unavailable();return;}
  const bool scrape=value.kind==QueueTrackerTerminalKind::Scrape||value.kind==QueueTrackerTerminalKind::ScrapeFailed;
  const bool failed=value.kind==QueueTrackerTerminalKind::ScrapeFailed||value.kind==QueueTrackerTerminalKind::AnnounceFailed;
  if(scrape){pending_=false;regular_=true;const auto delay=value.regularDelayMillis.value_or(600000);
    nextRegular_=delay>0&&value.observedMillis>INT64_MAX-delay?INT64_MAX:value.observedMillis+delay;}
  if(failed){
    if(!value.definitiveFailure){known_=false;return;}
    if(scrape){error_=true;visible_=true;} // Announce failure does not change the BT scraper object.
    return;
  }
  if(!value.comparable){known_=false;return;}
  const auto seeds=(value.present&1)?trackerInt(value.complete):0;
  const auto peers=(value.present&2)?trackerInt(value.incomplete):0;
  const auto downloaded=(value.present&4)?trackerInt(value.downloaded):-1;
  if(scrape){
    if(seeds<0||peers<0||downloaded< -1){error_=true;visible_=true;return;}
    current_={seeds,peers,value.requestStartedWallMillis};error_=false;visible_=true;known_=true;
  }else if(value.present&&seeds>=0&&peers>=0){
    current_={seeds,peers,value.observedWallMillis};error_=false;visible_=true;known_=true;
  }
}
bool QueueOriginalTrackerResponse::publishDht(std::int32_t seeds,std::int32_t peers,std::int64_t wall) noexcept {
  if(!known_||(!error_&&!backup_)||seeds<0||peers<0||(!seeds&&!peers)){return false;}
  current_={seeds,peers,wall};visible_=true;error_=false;backup_=true;ownStart_=wall;return true;
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
  result.durations=QueueObservedDurations{down,seed};result.idleEpoch=idleEpoch_;
  result.lastTrackerScrape=lastTrackerScrape_;result.singleTrackerResponse=singleTrackerResponse_;
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
  if(raw>=6&&raw<=9){
    if(orderKnown_&&count_==1&&scrapes_[0].key==event.tracker){
      if(!singleTrackerResponse_){singleTrackerResponse_=QueueSingleTrackerResponse{};}
      auto& source=*singleTrackerResponse_;source.tracker=event.tracker;
      source.sequence=event.sequence;source.request=event.request;source.protocol=event.protocol;
      if(kind==QueueObservationKind::Announce&&event.seeds>=0&&event.peers>=0){
        // Original announce parsing writes a valid scraper response when both
        // counts are nonnegative. The callback's receive clock is the source
        // input counterpart, not an independently observed Java wall clock.
        source.response=sourceQueueOriginalScrapeResponse(QueueOriginalScrapeContent{
            event.seeds,event.peers,event.observedWallMillis});
      }else if(kind==QueueObservationKind::Scrape||kind==QueueObservationKind::Announce){
        // Missing/explicit-1 and downloaded presence/value are indistinguishable
        // here. Original may retain counts or create a new valid response. Once
        // valid it stays valid, but exact counts/start cannot be reconstructed.
        if(source.response.type!=QueueOriginalScrapeType::Success){source.response.type=QueueOriginalScrapeType::Unknown;}
        source.response.content.reset();
      }
      // RequestFailed changes status/retry time, not retained response counts.
      // Unknown is deliberately not converted to original RT_ERROR.
    }else{singleTrackerResponse_.reset();}
  }
  if(kind==QueueObservationKind::StartedDownload||kind==QueueObservationKind::StartedSeed){
    if(started_){lost_=true;return false;}
    const auto epoch=idleEpoch_?idleEpoch_->number:0;
    if(epoch==std::numeric_limits<std::uint64_t>::max()){lost_=true;return false;}
    idleEpoch_=QueueIdleEpoch{epoch+1,event.observedWallMillis,{}};
    started_=now;startedWall_=event.observedWallMillis;
    if(kind==QueueObservationKind::StartedSeed){seedStarted_=now;seedStartedWall_=event.observedWallMillis;}
    else{seedStarted_.reset();seedStartedWall_.reset();}
  }else if(kind==QueueObservationKind::Finished){if(started_&&!seedStarted_){seedStarted_=now;seedStartedWall_=event.observedWallMillis;}}
  else if(kind==QueueObservationKind::Unfinished){seedStarted_.reset();seedStartedWall_.reset();}
  else if(kind==QueueObservationKind::Stopped){
    if(started_&&idleEpoch_){idleEpoch_->stoppedWallMillis=event.observedWallMillis;}
    const auto current=facts(now);
    if(!current.durations){lost_=true;return false;}
    downloadBase_=current.durations->downloading;seedBase_=current.durations->seeding;
    started_.reset();seedStarted_.reset();startedWall_.reset();seedStartedWall_.reset();
  }else if(kind==QueueObservationKind::Announce){
    announceSuccess_=true;announceSeeds_=event.seeds;announcePeers_=event.peers;
  }else if(kind==QueueObservationKind::AnnounceFailed){announceSuccess_=false;}
  else if(kind==QueueObservationKind::Scrape||kind==QueueObservationKind::ScrapeFailed){
    // The newest selected-protocol terminal supersedes the previous raw value.
    // Unknown/config-changed URL is unavailable, never a borrowed old source.
    lastTrackerScrape_.reset();
    unsigned index=0;for(;index<count_&&scrapes_[index].key!=event.tracker;++index){}
    if(index==count_){
      if(orderKnown_){return true;} // Not an original configured scrape source.
      // Without authenticated original order, equal-count ties cannot safely
      // acquire arrival-order authority. Do not guess an alternative order.
      lost_=true;return false;
    }
    lastTrackerScrape_=QueueScrapeTerminal{event.sequence,event.request,event.observedNanos,event.requestStartedNanos,
        event.observedWallMillis,event.requestStartedWallMillis,event.seeds,event.peers,event.protocol,event.tracker,
        kind==QueueObservationKind::Scrape?QueueScrapeTerminalKind::Reply:QueueScrapeTerminalKind::RequestFailed};
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
namespace {
std::int32_t idleInt(std::int64_t value) noexcept {return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
std::int32_t idleAdd(std::int32_t value,std::int64_t now,std::int64_t start) noexcept {
  if(value<0){return value;}
  const auto elapsed=std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(now)-static_cast<std::uint64_t>(start));
  return idleInt(sourceQueueSum(value,std::max<std::int64_t>(0,elapsed)/1000));
}
}
std::int32_t sourceQueuePeerIdleSeconds(std::int64_t wall,std::int32_t& last) noexcept {
  if(last==0){return -1;}
  const auto now=idleInt(wall/1000);if(now<last){last=now;}
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now)-static_cast<std::uint32_t>(last));
}
QueueIdleUploadValue QueueIdleUploadAge::observe(const QueueIdleUploadSample& value) noexcept {
  if(lost_||!value.admission||!value.sequence||!value.epoch.number||value.rawPayloadSent<0
      ||value.cumulativePayloadSent<value.rawPayloadSent){return {};}
  if(admission_&&(admission_!=value.admission||value.cumulativePayloadSent<cumulative_)){
    lost_=true;return {};
  }
  if(value.sequence<=sequence_){return {};}
  if(epoch_&&value.epoch.number<epoch_){lost_=true;return {};}
  const bool changed=value.epoch.number!=epoch_;
  if(changed){
    if(epoch_){baseKnown_=stopped_&&baseKnown_&&value.epoch.number==epoch_+1;}
    else if(value.epoch.number!=1){baseKnown_=false;}
    sourceLastSent_.reset();lastSentSeconds_=0;stopped_=false;raw_=0;
  }else if(value.rawPayloadSent<raw_){lost_=true;return {};}
  if(value.epoch.number==1&&value.cumulativePayloadSent!=value.rawPayloadSent){lost_=true;return {};}
  const auto previousRaw=raw_,previousCumulative=cumulative_;
  const bool previouslyStopped=stopped_;
  admission_=value.admission;sequence_=value.sequence;epoch_=value.epoch.number;
  raw_=value.rawPayloadSent;cumulative_=value.cumulativePayloadSent;
  const bool active=!value.epoch.stoppedWallMillis;
  if(previouslyStopped&&!active){
    if(previousCumulative!=cumulative_){baseKnown_=false;}
    return baseKnown_?QueueIdleUploadValue{true,base_}:QueueIdleUploadValue{};
  }
  const auto finishBase=[&](std::int64_t wall)->QueueIdleUploadValue {
    return baseKnown_?QueueIdleUploadValue{true,idleAdd(base_,wall,value.epoch.startedWallMillis)}:QueueIdleUploadValue{};
  };
  const auto peerIdle=[&](std::int64_t wall)->QueueIdleUploadValue {
    if(!sourceLastSent_){return {};}
    const auto age=sourceQueuePeerIdleSeconds(wall,lastSentSeconds_);
    return age>=0?QueueIdleUploadValue{true,age}:finishBase(wall);
  };
  QueueIdleUploadValue result;
  if(active){
    if(raw_>0){
      if(!value.lastPayloadWallMillis){return {};}
      if(!sourceLastSent_||raw_>previousRaw){sourceLastSent_=value.lastPayloadWallMillis;lastSentSeconds_=idleInt(*sourceLastSent_/1000);}
      else if(sourceLastSent_!=value.lastPayloadWallMillis){lost_=true;return {};}
      result=peerIdle(value.nowWallMillis);
    }else{result=finishBase(value.nowWallMillis);}
  }else{
    const auto stopped=*value.epoch.stoppedWallMillis;
    // A same-epoch accepted active sample must have observed every payload byte
    // before its actual stop. No reconstruction across a missed/reset epoch.
    if(raw_>0){
      if(changed||previousRaw!=raw_||previousCumulative!=cumulative_
          ||sourceLastSent_!=value.lastPayloadWallMillis){result={};}
      else{result=peerIdle(stopped);}
    }else{result=finishBase(stopped);}
    stopped_=true;baseKnown_=result.known;if(result.known){base_=result.seconds;}
  }
  return result;
}

}
