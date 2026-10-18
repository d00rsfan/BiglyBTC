#pragma once

#include "biglybtc/bencode.hpp"

#include <array>
#include <optional>
#include <span>

namespace biglybtc::core {

enum class LibraryError { Root, DownloadsType, Limit, EntryType, Path, Hash, Integer, PauseType };
enum class StartupState { Waiting = 0, Stopped = 70, Queued = 75 };

class TorrentHash final {
public:
  static std::expected<TorrentHash, LibraryError> parse(std::string_view value);
  std::span<const std::byte> bytes() const { return {value_.data(), length_}; }
  std::string hex() const;
  bool operator==(const TorrentHash&) const = default;
private:
  std::array<std::byte, 32> value_{};
  std::size_t length_ = 0;
};

struct SavedDownload {
  std::size_t sourceIndex = 0;
  std::optional<std::string> legacyKey;
  std::string torrentFile;
  std::string saveDirectory;
  std::optional<std::string> saveFile;
  bool legacySavePath = false;
  std::optional<TorrentHash> hash;
  std::int64_t originalState = 0;
  StartupState startup = StartupState::Waiting;
  bool persistent = true;
  bool forceStart = false;
  std::optional<std::int64_t> position;
  std::optional<std::int64_t> downloaded;
  std::optional<std::int64_t> uploaded;
  std::optional<std::int64_t> completedBytes;
  bool hasFilePriorities = false;
};

struct SavedPause {
  TorrentHash hash;
  bool forceStart;
  std::int64_t owner;
};
struct LibraryIssue {
  LibraryError error;
  std::size_t sourceIndex;
  bool pauseEntry;
};
struct LibrarySnapshot {
  std::vector<SavedDownload> downloads;
  std::vector<SavedPause> paused;
  std::vector<LibraryIssue> issues;
  bool legacyMap = false;
};
struct LibraryLimits {
  std::size_t downloads = 100000;
  std::size_t paused = 100000;
};

std::expected<LibrarySnapshot, LibraryError> readLibrary(const Document& document, LibraryLimits limits = {});

} // namespace biglybtc::core
