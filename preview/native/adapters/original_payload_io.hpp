// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "original_file_io.hpp"

namespace biglybtc::storage::detail {

struct OriginalPayloadStat {
  std::uint64_t device, inode, size, links, mode, owner;
  std::int64_t modified, modifiedFraction, changed, changedFraction;
  bool operator==(const OriginalPayloadStat&) const = default;
};

std::uint64_t originalPayloadMaximumOffset() noexcept;
bool statOriginalPayload(OriginalPayloadHandle, OriginalPayloadStat&) noexcept;
bool canWriteOriginalPayload(OriginalPayloadHandle) noexcept;
std::ptrdiff_t readOriginalPayload(OriginalPayloadHandle, std::uint64_t, std::span<std::byte>) noexcept;
std::ptrdiff_t writeOriginalPayload(OriginalPayloadHandle, std::uint64_t, std::span<const std::byte>) noexcept;
bool resizeOriginalPayload(OriginalPayloadHandle, std::uint64_t) noexcept;
bool syncOriginalPayload(OriginalPayloadHandle) noexcept;
std::expected<void, Error> enableSparseOriginalPayload(OriginalPayloadHandle);

} // namespace biglybtc::storage::detail
