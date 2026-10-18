// SPDX-License-Identifier: MPL-2.0
// Original mlDHT Node/KBucketEntry/AddressUtils format baseline:
// f9290cbd63805229aef418639b781696c5fa1573.
#include "mldht_cache.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
namespace biglybtc::storage {
namespace {
using Node = core::Node;
constexpr std::size_t MaximumEntries = 100000;
std::optional<std::int64_t> integer(const Node* value) {
  if (value && value->type == Node::Type::Integer) { return value->integer; }
  return std::nullopt;
}
std::optional<std::string_view> bytes(const Node* value) {
  if (value && value->type == Node::Type::Bytes) { return value->bytes; }
  return std::nullopt;
}
std::expected<MldhtRoutingEntry, MldhtCacheError> entry(const Node& node, std::size_t index,
    bool replacement, std::int64_t now) {
  const auto address = bytes(node.find("addr")), id = bytes(node.find("id"));
  if (!address || (address->size() != 6 && address->size() != 18) || !id || id->size() != 20) {
    return std::unexpected(MldhtCacheError{"Invalid original routing address or node key", index});
  }
  MldhtRoutingEntry result{.sourceIndex = index, .replacement = replacement};
  std::memcpy(result.id.data(), id->data(), 20);
  result.family = address->size() == 6 ? 4 : 6;
  const auto count = address->size() - 2;
  std::memcpy(result.address.data(), address->data(), count);
  if (result.family == 6 && std::all_of(result.address.begin(), result.address.begin() + 10,
      [](auto value) { return value == 0; }) && result.address[10] == 255 && result.address[11] == 255) {
    // Original unpackAddress uses InetAddress.getByAddress, which normalizes a
    // mapped address. originalBytes still preserves the stored18-byte encoding.
    std::copy_n(result.address.begin() + 12, 4, result.address.begin());
    std::fill(result.address.begin() + 4, result.address.end(), 0);
    result.family = 4;
  }
  result.port = static_cast<std::uint16_t>((static_cast<unsigned char>((*address)[count]) << 8)
      | static_cast<unsigned char>((*address)[count + 1]));
  result.created = integer(node.find("created")).value_or(now);
  result.lastSeen = integer(node.find("lastSeen")).value_or(now);
  result.lastSend = integer(node.find("lastSend")).value_or(-1);
  const auto failures = integer(node.find("failedCount")).value_or(0);
  result.failedCountRaw = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(failures));
  // Java Math.abs(Integer.MIN_VALUE) returns Integer.MIN_VALUE. Avoid C++ UB.
  result.failedCount = result.failedCountRaw < 0 && result.failedCountRaw != std::numeric_limits<std::int32_t>::min()
      ? -result.failedCountRaw : result.failedCountRaw;
  result.verified = integer(node.find("verified")).value_or(0) == 1;
  result.version = bytes(node.find("version"));
  return result;
}
}
std::expected<OriginalMldhtCache, MldhtCacheError> OriginalMldhtCache::parse(std::string input, std::int64_t now) {
  auto parsed = core::Document::parse(std::move(input), {.inputBytes = 50 * 1024 * 1024,
      .stringBytes = 50 * 1024 * 1024, .nodes = 2000000, .depth = 64});
  if (!parsed || parsed->root().type != Node::Type::Dictionary) {
    return std::unexpected(MldhtCacheError{"Invalid or excessive original routing cache"});
  }
  OriginalMldhtCache result(std::move(*parsed));
  const auto& document = result.document_;
  if (const auto key = bytes(document.find("oldKey")); key && key->size() == 20) {
    std::array<std::uint8_t, 20> value{};
    std::memcpy(value.data(), key->data(), value.size()); result.oldKey_ = value;
  }
  if (const auto estimate = bytes(document.find("log2estimate")); estimate && estimate->size() == 8) {
    std::uint64_t value = 0;
    for (const unsigned char byte : *estimate) { value = (value << 8) | byte; }
    result.estimateBits_ = value;
  }
  result.timestamp_ = integer(document.find("timestamp")).value_or(-1);
  for (const auto name : {"mainEntries", "replacements"}) {
    const auto* list = document.find(name);
    if (!list || list->type != Node::Type::List) { continue; }
    for (std::size_t index = 0; index < list->children.size(); ++index) {
      const auto& node = list->children[index];
      if (node.type != Node::Type::Dictionary) { continue; }
      if (result.entries_.size() == MaximumEntries) {
        return std::unexpected(MldhtCacheError{"Original routing entry limit exceeded", index});
      }
      auto decoded = entry(node, index, std::string_view(name) == "replacements", now);
      if (!decoded) { return std::unexpected(decoded.error()); }
      result.entries_.push_back(std::move(*decoded));
    }
  }
  return result;
}
}
