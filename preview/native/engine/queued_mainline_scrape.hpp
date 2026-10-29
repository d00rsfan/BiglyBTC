#pragma once
#include "mainline_dht.hpp"
#include "mainline_scrape_estimate.hpp"
#include "original_torrent_identity.hpp"
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace biglybtc::engine {
namespace detail {class ManagedStorage;}
// Borrowed only during this synchronous metadata authentication call. The sole
// owner supplies SAME-lease storage/token and its accepted checked active
// sample; this value never prepares storage or grants transfer/discovery rights.
struct ManagedMainlineScrapeObservation {
  const detail::ManagedStorage* storage=nullptr;
  std::uint64_t admission=0;
  bool wholeComplete=false;
};
// Passive query authority, never a payload claim or transfer permission. Scope
// binds selected raw/full identity, current policy and optional actual admission.
enum class SavedTrackerPurpose {MainlineBackup,RegularHttp};
struct SavedMainlineTracker {std::string url,responseKey;unsigned tier=0;};
struct SavedMainlineScrapeTarget {
  core::TorrentHash hash;
  storage::OriginalFullInfoHashes full;
  std::string scope;
  bool noSeeds=false;
  std::uint64_t admission=0; // Zero is explicit saved/unadmitted queued authority.
  std::string trackerResponseKey{}; // Empty is the established trackerless path.
  std::string configuredTracker{};
  // Original declared tier/URL order; the owner applies its retained Java seed.
  // Active announcer multi-URL selection remains outside this saved-only seam.
  std::vector<SavedMainlineTracker> configuredTrackers{};
};
// Resolve a member only from a freshly authenticated complete URL inventory.
std::optional<SavedMainlineScrapeTarget> selectConfiguredMainlineTracker(
    const SavedMainlineScrapeTarget&,std::string_view responseKey);
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error>
readSavedMainlineScrapeTargets(const storage::ProfileLease&);
// Combines saved queued75 with actual active Downloading/Seeding observations
// whose current explicit mlDHT enableannounce Java-int value is zero. Saved
// row state alone can never manufacture the active branch.
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error>
readCurrentMainlineScrapeTargets(const storage::ProfileLease&,
    std::span<const ManagedMainlineScrapeObservation>,bool configuredCandidates=false,
    std::vector<std::string>* authenticatedInventoryHashes=nullptr,
    SavedTrackerPurpose purpose=SavedTrackerPurpose::MainlineBackup);
// The optional inventory output is assigned only after successful full saved
// inventory authentication/CAS. It includes ineligible/stopped rows, allowing
// RAM-only manager lifetime cleanup without treating lost permission as removal.
// Pure injected monotone clock/random-word scheduling. No sampler or timer.
// Initial 60..119s, repeat 1200..1799s; check cadence10s, startup100s.
class QueuedMainlineScrapes final {
public:
  explicit QueuedMainlineScrapes(std::int64_t startedMillis=0)
      :checkAt_(startedMillis<0||startedMillis>INT64_MAX-100000?INT64_MAX:startedMillis+100000){}
  static std::int64_t delayMillis(bool initial,std::uint32_t random) noexcept;
  bool checkDue(std::int64_t now) noexcept;
  std::expected<void,storage::Error> reconcile(std::span<const SavedMainlineScrapeTarget>,
      std::int64_t now,std::uint32_t random);
  std::optional<SavedMainlineScrapeTarget> due(std::int64_t now) const;
  bool started(const core::TorrentHash&,std::string_view scope,std::uint64_t round,std::int64_t wall);
  void completed(std::uint64_t,const std::optional<MainlineDhtScrape>&,
      const std::optional<SavedMainlineScrapeTarget>& fresh,std::int64_t now,std::uint32_t random);
  void revoke(const core::TorrentHash&) noexcept;
  bool revoked(const core::TorrentHash&)const noexcept;
  void settled(const core::TorrentHash&); // Explicit freshly authenticated saved-command success.
  void unavailable() noexcept {rows_.clear();} // Failed read never clears tombstones.
  std::vector<MainlineScrapeEstimate> estimates() const;
  std::optional<std::uint64_t> active()const noexcept{return active_;}
  std::optional<SavedMainlineScrapeTarget> activeTarget()const;
private:
  struct Item {SavedMainlineScrapeTarget target;std::int64_t due=0,wall=0;std::optional<MainlineScrapeEstimate> value;};
  std::map<std::string,Item> rows_;
  std::map<std::string,std::string> revoked_;
  std::optional<std::uint64_t> active_;
  std::string activeKey_,activeScope_;
  std::int64_t checkAt_=0,lastNow_=0;
  bool revokedOverflow_=false;
};
} // namespace biglybtc::engine
