// SPDX-License-Identifier: GPL-2.0-or-later
#include "rpc_identity_store.hpp"

#include "plugin_attributes.hpp"
#include "torrent_state.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_set>

namespace biglybtc::storage {
namespace {
constexpr std::string_view runtimeId = "xmwebui";
constexpr std::string_view attribute = "xmui.dl.id";
constexpr std::string_view attributeKey = "Plugin.xmwebui.xmui.dl.id";
constexpr std::string_view counter = "xmui.next.id";

std::unexpected<Error> conflict(const char* description) {
  return std::unexpected(Error{ErrorCode::Conflict, description});
}

bool uniqueKey(const core::Node& dictionary, std::string_view key) {
  bool found = false;
  for (std::size_t index = 0; index + 1 < dictionary.children.size(); index += 2) {
    if (dictionary.children[index].bytes != key) { continue; }
    if (found) { return false; }
    found = true;
  }
  return true;
}

std::expected<std::string, Error> assignedAttributes(
    const core::Node* map, const std::string& original, std::int64_t expected, std::int64_t reserved) {
  const auto existing = readOriginalPluginLongMap(map, runtimeId, attribute);
  if (!existing || *existing != expected || (map && !uniqueKey(*map, attributeKey))) {
    return conflict("The original RPC attribute is ambiguous");
  }
  const auto& raw = map && map->origin ? *map->origin : original;
  auto attributes = core::Document::parse(map ? raw.substr(map->begin, map->end - map->begin) : "de", compatibleConfigLimits());
  if (!attributes || !attributes->replaceValue(attributeKey, "i" + std::to_string(reserved) + "e")) {
    return conflict("Cannot prepare original RPC identity metadata");
  }
  return attributes->serialize();
}

struct ArchiveAssignment {
  const PersistedRpcIdentity* prior;
  std::int64_t next;
};

std::expected<void, Error> prepareArchiveIdentities(ConfigFile& archive, const std::vector<ArchiveAssignment>& assignments) {
  auto& document = archive.document();
  const auto& raw = document.originalBytes();
  const auto* stubs = document.find("stubs");
  if (archive.recoverySource() != RecoverySource::Primary || !stubs || stubs->type != core::Node::Type::List ||
      !uniqueKey(document.root(), "stubs")) {
    return conflict("A current original archive inventory is required");
  }
  std::map<std::size_t, std::string> changed;
  for (const auto& assignment : assignments) {
    const auto& identity = *assignment.prior;
    if (!identity.hash || !identity.id || identity.sourceIndex >= stubs->children.size()) {
      return conflict("The selected original archive entry is unavailable");
    }
    const auto& selected = stubs->children[identity.sourceIndex];
    const auto* key = selected.find("hash");
    if (selected.type != core::Node::Type::Dictionary || !key || key->type != core::Node::Type::Bytes ||
        core::TorrentHash::parse(key->bytes) != identity.hash || !uniqueKey(selected, "hash") || !uniqueKey(selected, "attr")) {
      return conflict("The selected original archive entry has changed");
    }
    auto attributes = assignedAttributes(selected.find("attr"), raw, *identity.id, assignment.next);
    if (!attributes) { return std::unexpected(attributes.error()); }
    auto replaced = core::Document::parse(raw.substr(selected.begin, selected.end - selected.begin), compatibleConfigLimits());
    if (!replaced || !replaced->replaceValue("attr", std::move(*attributes)) ||
        !changed.emplace(identity.sourceIndex, replaced->serialize()).second) {
      return conflict("Cannot prepare the selected original archive entry");
    }
  }
  std::string encoded = "l";
  for (std::size_t index = 0; index < stubs->children.size(); ++index) {
    const auto& stub = stubs->children[index];
    const auto replacement = changed.find(index);
    if (replacement != changed.end()) { encoded += replacement->second; }
    else { encoded.append(raw, stub.begin, stub.end - stub.begin); }
  }
  encoded += 'e';
  if (!document.replaceValue("stubs", std::move(encoded))) { return conflict("Cannot prepare the original archive inventory"); }
  return {};
}
} // namespace

std::expected<std::int64_t, Error> allocateRpcIdentity(const ProfileLease& lease,
    std::string originalPluginDirectory, const core::TorrentHash& hash, RpcIdentityMember member) {
  const bool archived = member == RpcIdentityMember::Archived;
  const auto access = lease.sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  const auto profile = readRpcProfileSnapshot(lease, std::move(originalPluginDirectory));
  if (!profile) { return std::unexpected(profile.error()); }
  if (!profile->nextId || *profile->nextId <= 0 || !profile->issues.empty() ||
      !profile->counterWrongTypes.empty() || !profile->library.inventoryIssues.empty() ||
      profile->library.cacheWarning || !profile->library.cacheIssues.empty()) {
    return conflict("Current RPC identity inventory and sequence are required");
  }
  const PersistedRpcIdentity* selected = nullptr;
  std::int64_t maximum = 0;
  std::vector<const PersistedRpcIdentity*> duplicates;
  std::unordered_set<std::int64_t> issued;
  for (const auto& row : profile->identities) {
    if (!row.hash || !row.id || row.origin == RpcIdOrigin::Backup) {
      return conflict("A saved RPC identity is unavailable or in recovery");
    }
    if (row.hash == hash) {
      if (selected || row.archived != archived) { return conflict("Select one current matching download"); }
      selected = &row;
    }
    if (*row.id > 0) {
      if (!issued.insert(*row.id).second) {
        const bool canEdit = archived ? row.archived : !row.archived && row.hash == hash;
        if (!canEdit) { return conflict("Other duplicate RPC identities require separate repair"); }
        duplicates.push_back(&row);
      }
      maximum = std::max(maximum, *row.id);
    }
  }
  if (!selected || (archived && profile->hideArchived)) { return conflict("The selected download is not visible"); }
  if (!duplicates.empty()) {
    std::map<std::string, unsigned, std::less<>> editedHashes;
    for (const auto* duplicate : duplicates) { editedHashes.try_emplace(duplicate->hash->hex(), 0); }
    for (const auto& row : profile->identities) {
      const auto found = editedHashes.find(row.hash->hex());
      if (found != editedHashes.end() && ++found->second > 1) {
        return conflict("A duplicate RPC identity has an ambiguous torrent hash");
      }
    }
  }
  if (!archived) {
    const auto bound = std::ranges::find_if(profile->library.items, [&](const auto& item) { return item.saved.hash == hash; });
    if (bound == profile->library.items.end() || !bound->saved.persistent ||
        bound->saved.startup != core::StartupState::Stopped ||
        std::ranges::any_of(profile->library.pauses, [&](const auto& pause) { return pause.hash == hash; })) {
      return conflict("Select a current stopped persistent download");
    }
  }
  if (*selected->id > 0 && duplicates.empty()) { return *selected->id; }
  const auto limit = std::numeric_limits<std::int64_t>::max();
  if (*selected->id < 0 || maximum == limit) {
    return conflict("The original RPC identity cannot be allocated");
  }
  auto next = std::max(maximum + 1, *profile->nextId);
  auto result = *selected->id;
  std::vector<ArchiveAssignment> assignments;
  // The original scan retains the first ID in active then archive order.
  // Use one archive commit or the selected stopped member's attribute commit.
  for (const auto* duplicate : duplicates) {
    if (next == limit) { return conflict("The original RPC identity sequence is exhausted"); }
    if (duplicate == selected) { result = next; }
    if (archived) { assignments.push_back({duplicate, next}); }
    ++next;
  }
  if (!result) {
    if (next == limit) { return conflict("The original RPC identity sequence is exhausted"); }
    result = next++;
    if (archived) { assignments.push_back({selected, result}); }
  }

  auto configuration = ConfigFile::load(lease, "biglybt.config");
  if (!configuration) { return std::unexpected(configuration.error()); }
  if (configuration->recoverySource() != RecoverySource::Primary) {
    return conflict("The original RPC sequence is in configuration recovery");
  }
  auto archive = ConfigFile::load(lease, "dlarchive.config");
  if ((!archive && archive.error().code != ErrorCode::Missing) ||
      (archive && archive->recoverySource() != RecoverySource::Primary)) {
    return conflict("The original RPC archive inventory is in recovery");
  }
  const auto counterKey = profile->binding.configPrefix + std::string(counter);
  if (!uniqueKey(configuration->document().root(), counterKey) ||
      !configuration->document().replaceValue(counterKey, "i" + std::to_string(next) + "e")) {
    return conflict("The original RPC sequence is ambiguous");
  }

  // Publish the reservation first so a failed member write leaves a gap.
  if (archived) {
    if (!archive) { return std::unexpected(archive.error()); }
    const auto prepared = prepareArchiveIdentities(*archive, assignments);
    if (!prepared) { return std::unexpected(prepared.error()); }
    const auto reservation = configuration->save(lease);
    if (!reservation) { return std::unexpected(reservation.error()); }
    auto saved = archive->save(lease);
    if (!saved) { return std::unexpected(saved.error()); }
    return result;
  }

  auto store = TorrentStateStore::open(lease);
  if (!store) { return std::unexpected(store.error()); }
  auto state = store->loadAttributes(lease, hash);
  if (!state) { return std::unexpected(state.error()); }
  if (state->recoveredBackup()) { return conflict("The original torrent attributes are in recovery"); }
  auto attributes = assignedAttributes(state->document().find("attributes"), state->document().originalBytes(),
      *selected->id, result);
  if (!attributes) { return std::unexpected(attributes.error()); }
  const auto reservation = configuration->save(lease);
  if (!reservation) { return std::unexpected(reservation.error()); }
  const auto saved = store->saveAttributes(lease, *state, std::move(*attributes));
  if (!saved) { return std::unexpected(saved.error()); }
  return result;
}

}
