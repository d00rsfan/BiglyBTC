#pragma once
#include "biglybtc/library.hpp"
#include "profile.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace biglybtc::engine {
enum class ManagedPhase { Checking, Queued, Downloading, Seeding, Paused, Stopping, Stopped, Failed };
struct ManagedDownloadStatus {
  core::TorrentHash hash;
  std::uint64_t admission = 0;
  ManagedPhase phase = ManagedPhase::Checking;
  std::int64_t completed = 0, bytes = 0, received = 0, sent = 0;
  int downloadRate = 0, uploadRate = 0, pieces = 0, totalPieces = 0;
  bool complete = false;
  bool retained = false;
  bool checked = false, selectedComplete = false;
  std::optional<storage::Error> failure{};
};
struct ManagedDownloadOptions {
  core::TorrentHash hash;
  unsigned short ownedLoopbackPeerPort = 0;
  bool forceStart = false;
  bool queued = false;
};
} // namespace biglybtc::engine
