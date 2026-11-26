// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "saved_rss_source.hpp"
#include "subscription_seed_values.hpp"
#include <vector>

namespace biglybtc::search {
enum class SubscriptionSeedNetwork { Public, I2P };
enum class SubscriptionSeedKey { Publication, DdbLookup };

struct SavedSubscriptionSeed {
  SubscriptionSeed metainfo;
  std::string payload;
};

struct PreparedSubscriptionSeed {
  SubscriptionSeed metainfo;
  std::filesystem::path directory;
};

struct SavedSubscriptionSeedCandidate {
  std::array<char,20> hash;
  std::string publicKey;
  std::int64_t popularity;
};

// Lightweight catalog observation. The signed body is checked on demand.
std::expected<std::vector<SavedSubscriptionSeedCandidate>,SavedRssSourceError> savedSubscriptionSeedCandidates(
    const storage::ProfileLease&,SubscriptionSeedNetwork);

// Resolve one still-subscribed signed body through the currently held profile.
// Caller retains the selected route and decides when to register the seed.
std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease&,std::string_view key,SubscriptionSeedKey,SubscriptionSeedNetwork,
    std::int64_t creationSeconds);
std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease&,const storage::ProfileSourceAccess&,std::string_view key,SubscriptionSeedKey,
    SubscriptionSeedNetwork,std::int64_t creationSeconds);
std::expected<std::optional<PreparedSubscriptionSeed>,SavedRssSourceError> prepareSavedSubscriptionSeed(
    const storage::ProfileLease&,std::string_view publicationHash,SubscriptionSeedNetwork,
    std::int64_t creationSeconds);
} // namespace biglybtc::search
