#pragma once
#include "mainline_dht.hpp"
#include "mainline_scrape_estimate.hpp"
#include "original_torrent_identity.hpp"
#include <map>
#include <optional>
#include <span>

namespace biglybtc::engine {
// Read-only library authority, never a torrent admission/payload claim. Scope is
// a digest of actual selected raw/full identity and current saved permission.
struct SavedMainlineScrapeTarget {
  core::TorrentHash hash;
  storage::OriginalFullInfoHashes full;
  std::string scope;
  bool noSeeds=false;
};
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error>
readSavedMainlineScrapeTargets(const storage::ProfileLease&);
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
