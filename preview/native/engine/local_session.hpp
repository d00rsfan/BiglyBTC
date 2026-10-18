#pragma once

#include "biglybtc/library.hpp"
#include "profile.hpp"

#include <chrono>
#include <functional>
#include <stop_token>

namespace biglybtc::engine {

enum class LocalStage { StatePrepared, Checked, Flushed, CompletionLinked, CompletionSaved, InventorySaved };
struct LocalProgress {
  enum class Phase { Checking, Downloading, Flushing } phase;
  std::int64_t completedBytes;
  std::int64_t downloadRate;
  std::int64_t uploadRate;
  int pieces;
  int totalPieces;
};
struct LocalOptions {
  std::filesystem::path profile;
  core::TorrentHash hash;
  unsigned short peerPort;
  std::chrono::seconds timeout{90};
  std::stop_token stop{};
  void (*observer)(LocalStage) = nullptr;
  std::function<void(const LocalProgress&)> progress{};
};
struct LocalReport {
  int reusedPieces;
  std::int64_t received;
  std::int64_t bytes;
  std::filesystem::path payload;
};
struct TorrentFileDetails {
  int index;
  std::string path;
  std::int64_t bytes;
  std::int32_t priority;
  bool skipped;
  bool padding;
};
struct TorrentDetails {
  std::string name;
  std::optional<std::string> displayName;
  std::int64_t bytes;
  int files;
  int pieces;
  std::vector<TorrentFileDetails> entries{};
};

// Experimental linear continuation. No discovery, tracker, web-seed,
// proxy, router or external endpoint is enabled. Unsupported policy/layout is an
// explicit error; production multi-network session support remains separate work.
std::expected<LocalReport, storage::Error> continueLocal(const LocalOptions& options);
std::expected<LocalReport, storage::Error> continueLocal(const storage::ProfileLease& lease,
    const LocalOptions& options);
std::expected<TorrentDetails, storage::Error> queryTorrent(const storage::ProfileLease& lease,
    const core::TorrentHash& hash);

} // namespace biglybtc::engine
