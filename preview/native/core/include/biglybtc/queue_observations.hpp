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
// Exact terminal of one actually observed scrape RPC, selected protocol and
// configured URL digest. This is NOT Download.getLastScrapeResult/RT_ERROR:
// original retained counts, announce-created responses and tracker selection
// need separate authority. No saved aggregate/cache can construct this value.
enum class QueueScrapeTerminalKind { Reply, RequestFailed };
struct QueueScrapeTerminal {
  std::uint64_t sequence=0,request=0;
  std::int64_t observedNanos=0,requestStartedNanos=0;
  std::int64_t observedWallMillis=0,requestStartedWallMillis=0;
  std::int32_t seeds=-1,peers=-1;
  unsigned protocol=0;
  std::array<unsigned char,32> tracker{};
  QueueScrapeTerminalKind kind=QueueScrapeTerminalKind::RequestFailed;
};
// Literal DownloadScrapeResultImpl projection. Unknown means no authenticated
// original response, not RT_ERROR. The pure reducer never reads a cache/clock.
enum class QueueOriginalScrapeType { Unknown, Error, Success };
struct QueueOriginalScrapeContent {
  std::int32_t seeds=-1,peers=-1;
  std::int64_t startedWallMillis=-1;
};
struct QueueOriginalScrapeResponse {
  QueueOriginalScrapeType type=QueueOriginalScrapeType::Unknown;
  std::optional<QueueOriginalScrapeContent> content;
};
QueueOriginalScrapeResponse sourceQueueOriginalScrapeResponse(
    const std::optional<QueueOriginalScrapeContent>&) noexcept;
bool sourceQueueQueuedScrapeEligible(const QueueOriginalScrapeResponse&,
    const std::optional<std::int64_t>& ownStartedWallMillis) noexcept;
// Single configured-URL counterpart established only from this admission's
// selected-protocol terminal stream. Current96-byte callbacks cannot prove an
// initially null/invalid original response, nor all scrape/default transforms.
// This per-URL value is NOT Download.getLastScrapeResult selection authority.
// Rich protocol fields belong to a library/URL response object, not an
// admission. Construction mirrors a new original BT scraper response; only an
// actual regular-scrape terminal can arm configured Mainline fallback.
enum class QueueTrackerTerminalKind { Scrape,Announce,ScrapeFailed,AnnounceFailed };
struct QueueTrackerTerminal {
  QueueTrackerTerminalKind kind=QueueTrackerTerminalKind::ScrapeFailed;
  std::int64_t complete=0,incomplete=0,downloaded=0;
  unsigned present=0;
  bool comparable=false,definitiveFailure=false;
  std::int64_t observedMillis=0,observedWallMillis=0,requestStartedWallMillis=0;
};
class QueueOriginalTrackerResponse final {
public:
  void observe(const QueueTrackerTerminal&) noexcept;
  void unavailable() noexcept {known_=false;pending_=false;}
  bool eligible(std::int64_t nowMillis) const noexcept;
  bool regularDue(std::int64_t nowMillis) const noexcept;
  void requested() noexcept {pending_=true;}
  bool publishDht(std::int32_t seeds,std::int32_t peers,std::int64_t startedWallMillis) noexcept;
  QueueOriginalScrapeResponse response() const noexcept;
  bool dhtBackup()const noexcept{return backup_;}
  void forgetDhtAuthority()noexcept{ownStart_.reset();}
private:
  QueueOriginalScrapeContent current_;
  bool known_=true,visible_=false,backup_=false,error_=false;
  bool regular_=false,pending_=false;
  std::int64_t nextRegular_=0;
  std::optional<std::int64_t> ownStart_;
};
struct QueueSingleTrackerResponse {
  std::array<unsigned char,32> tracker{};
  std::uint64_t sequence=0,request=0;
  unsigned protocol=0;
  QueueOriginalScrapeResponse response;
};
struct QueueObservedFacts {
  std::optional<QueueObservedSwarm> swarm;
  std::optional<QueueIdleEpoch> idleEpoch;
  std::optional<QueueObservedDurations> durations;
  std::optional<std::int64_t> runningMillis,seedingMillis;
  // Passive same-admission raw source, never scrape-only network permission.
  std::optional<QueueScrapeTerminal> lastTrackerScrape;
  std::optional<QueueSingleTrackerResponse> singleTrackerResponse;
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
  std::optional<QueueScrapeTerminal> lastTrackerScrape_;
  std::optional<QueueSingleTrackerResponse> singleTrackerResponse_;
  bool aggregateCached_=false,everSuccess_=false,announceSuccess_=false,lost_=false;
  std::int32_t announceSeeds_=-1,announcePeers_=-1;
  std::uint64_t sequence_=0;
  std::int64_t lastMillis_=0,downloadBase_=0,seedBase_=0;
  std::optional<std::int64_t> started_,seedStarted_;
  std::optional<std::int64_t> startedWall_,seedStartedWall_;
  std::optional<QueueIdleEpoch> idleEpoch_;
};
}
