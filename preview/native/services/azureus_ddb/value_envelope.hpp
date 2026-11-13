// SPDX-License-Identifier: GPL-2.0-or-later
// Normal-read DDBaseImpl.listenerMapper behavior, pinned original cb8c6cb9.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace biglybtc::services::azureus_ddb {
enum class Error {Limit};
enum class Fault {None,MissingHeader,TruncatedLength,LengthExceedsPayload};
struct ValueRange {std::uint16_t offset=0,length=0;};
struct ValueEnvelope {
 std::array<ValueRange,256> entries{};std::uint16_t count=0;
 bool packed=false,continuation=false;Fault fault=Fault::None;
};
// Entry ranges borrow the caller's immutable raw bytes. No allocation/IO/hash,
// listener/network completion or source permission. Only exact flags4 packs.
// Partial entries survive malformed tails as in the original normal reader.
std::expected<ValueEnvelope,Error> decodeNormalValue(std::uint8_t flags,std::span<const std::uint8_t>)noexcept;
}
