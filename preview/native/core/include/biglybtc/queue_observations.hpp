#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
namespace biglybtc::core {
// Exact Java-long arithmetic and int narrowing; physical byte counters remain
// independently validated. Negative results besides infinity−1 are legitimate.
std::int32_t sourceQueueShareRatio(std::int64_t downloaded,std::int64_t uploaded,
    std::int64_t discarded,std::int64_t failed) noexcept;
std::int64_t sourceQueueSum(std::int64_t,std::int64_t) noexcept;
enum class QueueObservationKind : std::uint32_t {
  StartedDownload=1,StartedSeed=2,Finished=3,Unfinished=4,Stopped=5,
  Scrape=6,Announce=7,ScrapeFailed=8,AnnounceFailed=9,Lost=10
};
struct QueueObservation {
  std::uint64_t sequence=0,request=0;
  std::int64_t observedNanos=0,requestStartedNanos=0;
  std::int64_t observedWallMillis=0,requestStartedWallMillis=0;
  std::int32_t seeds=-1,peers=-1;
  QueueObservationKind kind=QueueObservationKind::Lost;
  unsigned protocol=0;
  std::array<unsigned char,32> tracker{};
};
struct QueueObservedDurations {std::int64_t downloading=0,seeding=0;};
struct QueueObservedSwarm {
  std::int32_t seeds=0,peers=0;
  bool success=false,startupSuccess=true;
};
struct QueueIdleEpoch {
  std::uint64_t number=0;
  std::int64_t startedWallMillis=0;
  std::optional<std::int64_t> stoppedWallMillis;
};
struct QueueIdleUploadSample {
  std::uint64_t admission=0,sequence=0;
  QueueIdleEpoch epoch;
  std::int64_t nowWallMillis=0,rawPayloadSent=0,cumulativePayloadSent=0;
  std::optional<std::int64_t> lastPayloadWallMillis;
};
struct QueueIdleUploadValue {bool known=false;std::int32_t seconds=-1;};
// Original wall/int32 idle arithmetic, independently of steady durations.
// A missed epoch with prior activity cannot acquire invented carryover.
std::int32_t sourceQueuePeerIdleSeconds(std::int64_t nowWallMillis,std::int32_t& lastSentSeconds) noexcept;
class QueueIdleUploadAge final {
public:
  explicit QueueIdleUploadAge(std::int32_t saved=-1) noexcept:base_(saved){}
  QueueIdleUploadValue observe(const QueueIdleUploadSample&) noexcept;
private:
  std::uint64_t admission_=0,sequence_=0,epoch_=0;
  std::int64_t cumulative_=0,raw_=0;
  std::int32_t base_=-1,lastSentSeconds_=0;
  std::optional<std::int64_t> sourceLastSent_;
  bool baseKnown_=true,stopped_=false,lost_=false;
};
struct QueueObservedFacts {
  std::optional<QueueObservedSwarm> swarm;
  std::optional<QueueIdleEpoch> idleEpoch;
  std::optional<QueueObservedDurations> durations;
  std::optional<std::int64_t> runningMillis,seedingMillis;
};
// Fixed source-owned cache, no clocks/IO/allocation. The creator supplies the
// selected protocol and consumes only events from that exact admission's sink.
class QueueOwnerObservations final {
public:
  static constexpr unsigned MaximumTrackers=64;
  explicit QueueOwnerObservations(unsigned selectedProtocol) noexcept:protocol_(selectedProtocol){}
  bool observe(const QueueObservation&) noexcept;
  bool trackerOrder(std::span<const std::array<unsigned char,32>>) noexcept;
  QueueObservedFacts facts(std::int64_t nowMillis) const noexcept;
  std::optional<QueueObservedDurations> settledDurations() const noexcept;
  // Original agsc contains updated epoch MINUTES,seeds,peers. It is older than
  // this fresh owner; never compare epoch minutes directly to monotone time.
  bool restoreAggregate(std::string_view) noexcept;
  void restoreLegacy(std::int64_t) noexcept;
  void lost() noexcept {lost_=true;}
private:
  struct Scrape {std::array<unsigned char,32> key{};std::int32_t seeds=-1,peers=-1;
    std::int64_t started=0;bool good=false;};
  unsigned protocol_=0,count_=0;bool orderKnown_=false;
  std::array<Scrape,MaximumTrackers> scrapes_{};
  std::optional<Scrape> aggregate_;
  bool aggregateCached_=false,everSuccess_=false,announceSuccess_=false,lost_=false;
  std::int32_t announceSeeds_=-1,announcePeers_=-1;
  std::uint64_t sequence_=0;
  std::int64_t lastMillis_=0,downloadBase_=0,seedBase_=0;
  std::optional<std::int64_t> started_,seedStarted_;
  std::optional<std::int64_t> startedWall_,seedStartedWall_;
  std::optional<QueueIdleEpoch> idleEpoch_;
};
}
