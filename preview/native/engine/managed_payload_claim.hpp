// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace biglybtc::engine::detail {
inline constexpr std::size_t MaximumManagedPayloadClaims=65536;
struct ManagedPayloadClaim {
  std::filesystem::path absolutePath;
  // inode0 means a reserved future path. Known inode claims never depend on
  // file size/mtime, which change during ordinary owned transfer.
  std::uint64_t device=0,inode=0;
};
} // namespace biglybtc::engine::detail
