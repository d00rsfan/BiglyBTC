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
struct SharedRatingSettings {
  bool pluginEnabled=true,dhtEnabled=true,autoChat=true;
  bool enabled()const noexcept{return pluginEnabled&&dhtEnabled;}
  bool operator==(const SharedRatingSettings&)const=default;
};
inline constexpr std::size_t MaximumDdbRatingBytes=509; // Original DHT512 minus3.
std::expected<SharedRatingSettings,Error> readSharedRatingSettings(const ProfileLease&);
struct DistributedRatingSnapshot {
  LocalRatingSnapshot local;
  std::array<std::uint8_t,32> sourceRevision{};
  std::array<std::uint8_t,27> key{};
};
// Original lookupRatingByHash does not require an owned download. This typed
// read subject carries no local contribution or publication/cache-write rights.
struct PublicRatingHashSnapshot {
 core::TorrentHash hash;std::array<std::uint8_t,32> sourceRevision{};
 std::array<std::uint8_t,27> key{};
};
std::expected<PublicRatingHashSnapshot,Error> readPublicRatingHash(const ProfileLease&,const core::TorrentHash&);
std::expected<DistributedRatingSnapshot,Error> readDistributedRating(const ProfileLease&,const core::TorrentHash&);
// Shared live owner: revalidates the same full identity/membership/Public gates
// plus the original plugin switch. Its domain-separated revision binds the
// rating contribution and full info, permitting unrelated live counter updates.
// The older explicit-reader revision above retains its full-document contract.
std::expected<DistributedRatingSnapshot,Error> readSharedDistributedRating(const ProfileLease&,const core::TorrentHash&);
// Actual derived-cache update only, never publication. Merges latest unknown
// fields under the existing full state/cache CAS and current shared source gate.
// Canonical owned Public download chat, independently of rating/plugin/score.
// Full original identity and saved membership are checked on every read. No
// identity/history creation or mutation, even for unscored downloads.
struct DownloadChatSnapshot {core::TorrentHash hash;std::string roomKey;std::array<std::uint8_t,32> sourceRevision{};};
// Config-only predicate shared by cold subject reads and fresh config-only memo checks.
std::expected<void,Error> validateDownloadChatConfig(const ProfileLease&);
std::expected<DownloadChatSnapshot,Error> readDownloadChat(const ProfileLease&,const core::TorrentHash&);
// Source-bound optional per-download chat notice; no identity/send authority.
struct RatingChatSnapshot {
 DistributedRatingSnapshot rating;std::string roomKey;bool written=false;
 std::array<std::uint8_t,32> sourceRevision{};
};
std::expected<RatingChatSnapshot,Error> readRatingChat(const ProfileLease&,const core::TorrentHash&);
std::expected<bool,Error> saveRatingChatWritten(const ProfileLease&,const core::TorrentHash&,
 const std::array<std::uint8_t,32>& expectedSource);
std::expected<bool,Error> saveSharedRatingAverage(const ProfileLease&,const core::TorrentHash&,
    const std::array<std::uint8_t,32>& expectedSource,float average);
std::expected<LocalRatingSnapshot, Error> readLocalRating(const ProfileLease&, const core::TorrentHash&);
std::expected<LocalRatingSaved, Error> saveLocalRating(const ProfileLease&, const core::TorrentHash&,
    const std::array<std::uint8_t, 32>& expectedAttributesRevision,
    std::int32_t score, std::u16string comment);
}
