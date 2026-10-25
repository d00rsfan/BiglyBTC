#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <array>
#include <cstddef>
#include <optional>
namespace biglybtc::storage {
struct OriginalFullInfoHashes {
  std::optional<std::array<std::byte,20>> v1;
  std::optional<std::array<std::byte,32>> v2;
  bool operator==(const OriginalFullInfoHashes&)const=default;
};
// Caller computes every present full hash from exactly rawInfo using its
// already-qualified engine/hash provider. This helper neither hashes nor
// substitutes/re-encodes info. Successful selection is not geometry validation.
struct ComputedOriginalInfoHashes {std::string_view rawInfo;OriginalFullInfoHashes hashes;};
enum class OriginalSelectedHash { V1, V2 };
struct OriginalTorrentIdentity {
  core::TorrentHash originalKey; // Always original 20-byte selected identity.
  OriginalSelectedHash selected;
  OriginalFullInfoHashes full;
};
// Borrowed original Document segment. Rejects replaced/rewritten info nodes.
std::expected<std::string_view,Error> originalTorrentInfoSubject(const core::Document&);
std::expected<OriginalTorrentIdentity,Error> selectOriginalTorrentIdentity(
    const core::Document&,const ComputedOriginalInfoHashes&);
// Read-only classification of the exact original info subject. The Java mode
// permits UTF16/String.compareTo order only inside the version2 file tree;
// all other dictionaries remain byte sorted. This never grants geometry or
// selected-swarm authority and never re-encodes/reorders any hashed byte.
enum class OriginalInfoOrder { ByteSorted, JavaUtf16FileTree };
std::expected<OriginalInfoOrder,Error> originalTorrentInfoOrder(const core::Document&);
// Explicit full32 is an opt-in reader compatibility policy, never the default
// key used when writing new original inventory/active/archive records.
std::expected<void,Error> validateOriginalTorrentKey(const OriginalTorrentIdentity&,
    const core::TorrentHash&,bool allowExplicitFullV2=false);
std::expected<void,Error> matchOriginalFullHashes(const OriginalFullInfoHashes& actual,
    const OriginalFullInfoHashes& requested);
enum class OriginalIdentityRelation { Distinct, SameSelected, AlternateSelected };
// Conflicting full evidence/prefix collisions fail. AlternateSelected identifies
// one full metainfo with two original keys; caller MUST NOT merge/co-admit LT
// handles implicitly. This classification grants no active-swarm permission.
std::expected<OriginalIdentityRelation,Error> compareOriginalTorrentIdentities(
    const OriginalTorrentIdentity&,const OriginalTorrentIdentity&);
}
