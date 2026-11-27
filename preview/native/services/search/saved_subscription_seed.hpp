// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "saved_rss_source.hpp"
#include "state_file_io.hpp"
#include "subscription_seed_values.hpp"
#include <memory>
#include <vector>

namespace biglybtc::search {
enum class SubscriptionSeedNetwork { Public, I2P };
enum class SubscriptionSeedKey { Publication, DdbLookup };
enum class SubscriptionSeedFile { Prepare, Verify };

struct SavedSubscriptionSeed {
  SubscriptionSeed metainfo;
  std::string payload;
};

struct SavedSubscriptionUpgradeProbe {
  std::string publicKey;
  std::vector<std::array<char,20>> associations;
  std::uint32_t minimumVersion=0;
};

struct SavedSubscriptionAssociationPublication {
  std::array<char,20> dhtKey;
  std::array<char,14> value;
  std::string applicationKey{};
  bool operator==(const SavedSubscriptionAssociationPublication&) const = default;
};

struct PreparedSubscriptionSeed {
  SubscriptionSeed metainfo;
  std::filesystem::path directory;
  std::shared_ptr<const storage::detail::StateDescriptor> directoryHandle;
  bool ddbTransferAllowed=false;
  std::optional<SavedSubscriptionUpgradeProbe> upgrade{};
  std::vector<SavedSubscriptionAssociationPublication> associations{};
};

struct SavedSubscriptionSeedCandidate {
  std::array<char,20> hash;
  std::string publicKey;
  std::int64_t popularity;
};

// Lightweight catalog observation. The signed body is checked on demand.
std::expected<std::vector<SavedSubscriptionSeedCandidate>,SavedRssSourceError> savedSubscriptionSeedCandidates(
    const storage::ProfileLease&,SubscriptionSeedNetwork);
// Observes a signed, subscribed row with is_public set and its current saved
// associations under the same profile lease and read hold as the seed.
std::expected<std::vector<SavedSubscriptionAssociationPublication>,SavedRssSourceError> savedSubscriptionAssociationPublications(
    const storage::ProfileLease&,const storage::ProfileSourceAccess&,std::string_view publicationHash,
    SubscriptionSeedNetwork,std::int64_t creationSeconds);

// Resolve one still-subscribed signed body through the currently held profile.
// Caller retains the selected route and decides when to register the seed.
std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease&,std::string_view key,SubscriptionSeedKey,SubscriptionSeedNetwork,
    std::int64_t creationSeconds);
std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease&,const storage::ProfileSourceAccess&,std::string_view key,SubscriptionSeedKey,
    SubscriptionSeedNetwork,std::int64_t creationSeconds);
// Verify binds an existing versioned file to the current signed body without writing.
std::expected<std::optional<PreparedSubscriptionSeed>,SavedRssSourceError> resolveVersionedSubscriptionSeed(
    const storage::ProfileLease&,std::string_view publicationHash,SubscriptionSeedNetwork,
    std::int64_t creationSeconds,SubscriptionSeedFile);
std::expected<std::optional<PreparedSubscriptionSeed>,SavedRssSourceError> resolveVersionedSubscriptionSeed(
    const storage::ProfileLease&,const storage::ProfileSourceAccess&,std::string_view publicationHash,
    SubscriptionSeedNetwork,std::int64_t creationSeconds,SubscriptionSeedFile);

// Host worker after the user accepts the verified version and its payload is present.
std::expected<void,SavedRssSourceError> installI2pSubscriptionUpgrade(
    const storage::ProfileLease&,const I2pSubscriptionUpgrade&,std::string definition);
std::expected<void,SavedRssSourceError> rememberI2pSubscriptionUpgrade(
    const storage::ProfileLease&,const I2pSubscriptionUpgrade&);
std::expected<void,SavedRssSourceError> recoverI2pSubscriptionUpgrade(const storage::ProfileLease&);
bool hasPendingI2pSubscriptionUpgrade(const storage::ProfileLease&);
} // namespace biglybtc::search
