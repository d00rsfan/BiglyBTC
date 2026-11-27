// SPDX-License-Identifier: GPL-2.0-or-later
// Source: BiglyBT cb8c6cb9, SubscriptionBodyImpl and DDBaseTTTorrent.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::core { struct Node; }

namespace biglybtc::search {
enum class SubscriptionSeedError { Ineligible, Malformed, Signature, Limit, Crypto };

struct SignedSubscriptionPresence {
  std::string applicationKey;
  std::string signature;
  std::array<char,20> publicationHash{};
  std::uint32_t version=0;
  std::uint32_t signedSize=0;
  std::array<char,20> dhtKey{};
};

struct SubscriptionSeed {
  std::string fileName, metainfo;
  std::array<char,20> publicationHash{}, lookupKey{};
  bool anonymous=false;
  std::optional<SignedSubscriptionPresence> presence;
};

// The persisted row and body must agree before this can provide metadata for
// a seed. The returned hash is the SHA-1 of signed details, as on Java's wire.
std::expected<SubscriptionSeed,SubscriptionSeedError> makeSubscriptionSeed(
    const core::Node& storedSubscription,std::string vuzeBytes,std::int64_t creationSeconds);
std::expected<std::string,SubscriptionSeedError> subscriptionSeedDdbResponse(const SubscriptionSeed&);
std::expected<std::string,SubscriptionSeedError> openSubscriptionSeedDdbResponse(
    std::string_view wire,const SignedSubscriptionPresence& expected);
std::expected<std::string,SubscriptionSeedError> encodeSubscriptionPresence(const SignedSubscriptionPresence&,
    std::int64_t publicationNonce);
std::expected<SignedSubscriptionPresence,SubscriptionSeedError> verifySubscriptionPresence(
    std::string_view wire,std::string_view expectedPublicKey,std::uint32_t expectedVersion);
// Recheck the downloaded Vuze file under the pinned key and route before
// passing it to a registry transaction.
std::expected<void,SubscriptionSeedError> validateSubscriptionSeedPayload(std::string_view,
    const SignedSubscriptionPresence&,std::string_view expectedPublicKey,bool anonymous);
} // namespace biglybtc::search
