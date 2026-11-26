// SPDX-License-Identifier: GPL-2.0-or-later
// Source: BiglyBT cb8c6cb9, SubscriptionBodyImpl and DDBaseTTTorrent.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace biglybtc::core { struct Node; }

namespace biglybtc::search {
enum class SubscriptionSeedError { Ineligible, Malformed, Signature, Limit, Crypto };

struct SubscriptionSeed {
  std::string fileName, metainfo;
  std::array<char,20> publicationHash{}, lookupKey{};
  bool anonymous=false;
};

// The persisted row and body must agree before this can provide metadata for
// a seed. The returned hash is the SHA-1 of signed details, as on Java's wire.
std::expected<SubscriptionSeed,SubscriptionSeedError> makeSubscriptionSeed(
    const core::Node& storedSubscription,std::string vuzeBytes,std::int64_t creationSeconds);
std::expected<std::string,SubscriptionSeedError> subscriptionSeedDdbResponse(const SubscriptionSeed&);
} // namespace biglybtc::search
