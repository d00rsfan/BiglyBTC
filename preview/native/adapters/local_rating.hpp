// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include "../plugins/BiglyBTC-plugin-azrating/local_rating.hpp"
#include <array>

namespace biglybtc::storage {
// Explicit relative include avoids the shared model's identical basename.
struct LocalRatingSnapshot {
  core::TorrentHash hash;
  plugins::rating::LocalRating local;
  std::array<std::uint8_t, 32> attributesRevision{};
  bool recoveredBackup = false;
};
struct LocalRatingSaved {
  LocalRatingSnapshot snapshot;
  bool changed = false;
  bool publicationSuggested = false;
  // Local-only host binding. Never reports a DDB write/queue as performed.
  bool publicationPerformed = false;
};
// Explicit Public-DDB target. The revision binds full original info bytes,
// selected identity, actual inventory, cached attributes/nickname and saved
// DHT availability config. No runtime/service or publication authority itself.
struct DistributedRatingSnapshot {
  LocalRatingSnapshot local;
  std::array<std::uint8_t,32> sourceRevision{};
  std::array<std::uint8_t,27> key{};
};
std::expected<DistributedRatingSnapshot,Error> readDistributedRating(const ProfileLease&,const core::TorrentHash&);
std::expected<LocalRatingSnapshot, Error> readLocalRating(const ProfileLease&, const core::TorrentHash&);
std::expected<LocalRatingSaved, Error> saveLocalRating(const ProfileLease&, const core::TorrentHash&,
    const std::array<std::uint8_t, 32>& expectedAttributesRevision,
    std::int32_t score, std::u16string comment);
}
