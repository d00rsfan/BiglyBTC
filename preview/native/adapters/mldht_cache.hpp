// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "biglybtc/bencode.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::storage {
struct MldhtCacheError { std::string message; std::size_t sourceIndex = 0; };
struct MldhtRoutingEntry {
  std::size_t sourceIndex = 0;
  bool replacement = false;
  std::array<std::uint8_t, 20> id{};
  std::uint32_t family = 0;
  std::array<std::uint8_t, 16> address{};
  std::uint16_t port = 0;
  std::int64_t created = 0, lastSeen = 0, lastSend = -1;
  std::int32_t failedCountRaw = 0, failedCount = 0;
  bool verified = false;
  std::optional<std::string_view> version{}; // borrowed from owning cache document
};
// Decoding is not admission to a routing table. The host must apply all original
// network/bind/privacy/eligibility rules before using stored contacts.
class OriginalMldhtCache final {
public:
  static std::expected<OriginalMldhtCache, MldhtCacheError> parse(std::string bytes, std::int64_t nowMilliseconds);
  const std::vector<MldhtRoutingEntry>& entries() const { return entries_; }
  const std::optional<std::array<std::uint8_t, 20>>& oldKey() const { return oldKey_; }
  const std::optional<std::uint64_t>& estimateBits() const { return estimateBits_; }
  std::int64_t timestamp() const { return timestamp_; }
  // No editor or disk writer exists in this bounded adapter. Unknown fields and
  // original encoded bytes, including floating-point payload bits, are retained.
  const std::string& originalBytes() const { return document_.originalBytes(); }
private:
  explicit OriginalMldhtCache(core::Document document) : document_(std::move(document)) {}
  core::Document document_;
  std::vector<MldhtRoutingEntry> entries_;
  std::optional<std::array<std::uint8_t, 20>> oldKey_;
  std::optional<std::uint64_t> estimateBits_;
  std::int64_t timestamp_ = -1;
};
}
