#pragma once

#include "biglybtc/library.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

namespace biglybtc::engine {

// Original v1 wizard sizes supported by the qualified generator (LT rejects
// the original final 256 MiB choice). Zero selects the original computed rule:
// first power of two from 32 KiB..4 MiB with floor(bytes / size) <= 2048.
inline constexpr std::array<std::uint32_t, 20> CreatePieceSizes{
    32768, 49152, 65536, 98304, 131072, 196608, 262144, 393216,
    524288, 786432, 1048576, 1572864, 2097152, 3145728, 4194304,
    8388608, 16777216, 33554432, 67108864, 134217728};
inline constexpr std::size_t CreateMaximumFiles = 8192;
inline constexpr std::size_t CreateMaximumDirectories = 2048;
inline constexpr std::size_t CreateMaximumDepth = 64;
inline constexpr std::size_t CreateMaximumPieces = 1048576;
inline constexpr std::uint64_t CreateMaximumPayloadBytes = std::uint64_t{1} << 44;
inline constexpr std::size_t CreateMaximumTrackerTiers = 16;
inline constexpr std::size_t CreateMaximumTrackers = 64;
inline constexpr std::size_t CreateMaximumTrackerBytes = 2048;
inline constexpr std::size_t CreateMaximumCommentBytes = 16384;

enum class CreateFormat { V1, V2, Hybrid };
// BEP52 requires powers of two, at least16KiB; LT2.0.12 permits at most128MiB.
inline constexpr std::array<std::uint32_t,14> CreateMerklePieceSizes{
    16384,32768,65536,131072,262144,524288,1048576,2097152,4194304,
    8388608,16777216,33554432,67108864,134217728};

struct CreateOptions {
  std::filesystem::path source;
  std::filesystem::path output;
  std::uint32_t pieceSize = 0;
  // Nonempty tiers; each URL must use http, https or udp and contain no
  // whitespace/control bytes. No tracker is contacted by this operation.
  std::vector<std::vector<std::string>> trackerTiers;
  bool privateTorrent = false;
  std::string comment;
  CreateFormat format = CreateFormat::V1; // Appended; legacy calls remain v1.
};

enum class CreateStage { Scanning, Hashing, Validating, Publishing };
struct CreateProgress {
  CreateStage stage;
  std::uint64_t completedBytes;
  std::uint64_t totalBytes;
  std::size_t files;
  std::size_t completedPieces;
  std::size_t totalPieces;
};
using CreateProgressCallback = std::function<void(const CreateProgress&)>;

enum class CreateErrorCode {
  Options, Path, UnsafeSource, Empty, Limit, Changed, Cancelled,
  Read, Write, Conflict, Generator, Callback, Memory
};
struct CreateError {
  CreateErrorCode code;
  std::filesystem::path path;
  std::error_code systemError;
  std::string message;
};
enum class CreatePublication { Durable, PublishedDirectorySyncFailed, PublishedCleanupFailed };
struct CreateResult {
  std::filesystem::path output;
  core::TorrentHash infoHashV1; // Actual v1 digest; empty only for explicit pure-v2.
  std::uint64_t totalBytes;
  std::size_t fileCount;
  std::uint32_t pieceSize;
  std::size_t pieceCount;
  CreatePublication publication;
  // Only populated after a post-publication sync/cleanup failure. The file exists;
  // callers must not automatically repeat creation or overwrite that file.
  std::error_code publicationError;
  CreateFormat format = CreateFormat::V1;
  core::TorrentHash selectedHash; // Original20 convention: v1 for hybrid, v2 prefix for pure-v2.
  std::optional<core::TorrentHash> fullInfoHashV1, fullInfoHashV2;
};

// Synchronous, caller-thread-only explicit v1/v2/hybrid creation. No session, network, import,
// profile mutation, payload writes or clock reads. Callbacks must marshal their
// own UI notifications; exceptions abort before publication. Cancellation is
// checked through the final pre-publication barrier; publication is indivisible.
// Requires absolute paths with no symlink component, an existing output parent
// outside a directory source, and exclusively owned/stable input while running.
// Observed file/directory identity, size, mtime and ctime changes are rejected;
// ordinary filesystem checks are not a filesystem snapshot against other writers.
std::expected<CreateResult, CreateError> createTorrent(const CreateOptions&,
    CreateProgressCallback progress = {}, std::stop_token stop = {});

} // namespace biglybtc::engine
