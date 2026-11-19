#pragma once
#include "biglybtc/file_policy.hpp"
#include "biglybtc/library.hpp"
#include "profile.hpp"
namespace biglybtc::storage {
// Borrowed explicit selection intent;never retained beyond the synchronous call.
struct FileSelectionIntent {std::span<const std::uint8_t> changed;bool skipped=false;};
// Caller first certifies retirement of this exact live handle. Original sparse
// priority encoding/no-op semantics remain for legacy callers. Explicit same-
// policy unskip carries its selected mask. Skip changes may relocate DND data
// through the existing durable FileOperation journal; recover it before reuse.
// Priority-only changes retain the ConfigFile/cache/resume transaction.
std::expected<void, Error> setFilePolicies(const ProfileLease&,const core::TorrentHash&,
    std::span<const compat::FilePolicy>,std::optional<FileSelectionIntent> intent={});
}
