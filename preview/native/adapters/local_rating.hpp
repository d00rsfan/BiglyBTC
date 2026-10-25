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
std::expected<LocalRatingSnapshot, Error> readLocalRating(const ProfileLease&, const core::TorrentHash&);
std::expected<LocalRatingSaved, Error> saveLocalRating(const ProfileLease&, const core::TorrentHash&,
    const std::array<std::uint8_t, 32>& expectedAttributesRevision,
    std::int32_t score, std::u16string comment);
}
