#pragma once

#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>

namespace biglybtc::storage {

enum class OriginalCompactionStage {
  JournalSynced, PayloadSynced, ControlPublished, StatePublished,
  InventoryPublished, Completed
};
struct OriginalCompactionResult {
  core::TorrentHash hash;
  std::size_t index = 0;
  std::uint64_t previousPhysicalBytes = 0, physicalBytes = 0;
  bool recovered = false;
};

// Serialized worker-only operation. Caller MUST retire every retained profile
// handle before calling this or recovery. No cache/session/pump is created.
// Explicit request only: ordinary Skip never invokes physical compaction.
// Accepts L only, marks this file skipped through setFilePolicies first. That
// preference may remain accepted on later failure; payload mutation starts only
// after a durable complete journal. All resume becomes unverified first.
// A post-journal failure requires recovery before any admission/Java handoff.
// Cancellation is honored until the complete journal is durably published.
std::expected<OriginalCompactionResult, Error> compactOriginalFile(
    const ProfileLease&, const core::TorrentHash&, std::size_t index,
    std::stop_token = {}, void (*observer)(OriginalCompactionStage) = nullptr);
bool hasPendingOriginalCompaction(const ProfileLease&) noexcept;
std::expected<std::optional<OriginalCompactionResult>, Error>
recoverOriginalCompaction(const ProfileLease&,
    void (*observer)(OriginalCompactionStage) = nullptr);

} // namespace biglybtc::storage
