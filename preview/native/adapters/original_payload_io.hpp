// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "original_file_io.hpp"
#ifndef _WIN32
#include "file_io.hpp"
#endif

namespace biglybtc::storage::detail {

#ifdef _WIN32
struct CloseOriginalPayload {
  void operator()(OriginalPayloadHandle) const noexcept;
};
using OriginalPayloadDescriptor = std::unique_ptr<void, CloseOriginalPayload>;
#else
using OriginalPayloadDescriptor = Descriptor;
#endif

enum class OriginalPayloadAccess { ReadOnly, ReadWrite, CreateExclusive };

// authenticatedLinks may be two only after the caller proves both journal aliases.
std::expected<OriginalPayloadDescriptor, Error> openOriginalPayloadAt(OriginalPayloadHandle directory,
    const std::filesystem::path& leaf, OriginalPayloadAccess, unsigned authenticatedLinks = 1);

struct OriginalPayloadStat {
  std::uint64_t device, inode, size, links, mode, owner;
  std::int64_t modified, modifiedFraction, changed, changedFraction;
  bool operator==(const OriginalPayloadStat&) const = default;
};

std::uint64_t originalPayloadMaximumOffset() noexcept;
bool statOriginalPayload(OriginalPayloadHandle, OriginalPayloadStat&) noexcept;
bool canWriteOriginalPayload(OriginalPayloadHandle) noexcept;
bool isOwnedReadOnlyOriginalPayload(OriginalPayloadHandle);
std::ptrdiff_t readOriginalPayload(OriginalPayloadHandle, std::uint64_t, std::span<std::byte>) noexcept;
std::ptrdiff_t writeOriginalPayload(OriginalPayloadHandle, std::uint64_t, std::span<const std::byte>) noexcept;
bool resizeOriginalPayload(OriginalPayloadHandle, std::uint64_t) noexcept;
bool syncOriginalPayload(OriginalPayloadHandle) noexcept;
std::expected<void, Error> enableSparseOriginalPayload(OriginalPayloadHandle);

} // namespace biglybtc::storage::detail
