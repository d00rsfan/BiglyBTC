// SPDX-License-Identifier: GPL-2.0-or-later
#include "rpc_profile.hpp"
#include "plugin_attributes.hpp"
#include "torrent_state.hpp"
#include <map>
namespace biglybtc::storage {
namespace {
constexpr std::string_view runtimeId = "xmwebui", attribute = "xmui.dl.id";
Error format(std::string message) { return {ErrorCode::InvalidFormat, std::move(message)}; }
Error cancelled() { return {ErrorCode::Cancelled, "Cancelled"}; }

std::int64_t integerField(const core::Node* fields, std::string_view key) {
  const auto* value = fields ? fields->find(key) : nullptr;
  return value && value->type == core::Node::Type::Integer ? value->integer : 0;
}

std::int64_t originalAddedMillis(const core::Node* attributes) {
  return integerField(attributes ? attributes->find("parameters") : nullptr, "stats.download.added.time");
}
}
std::expected<RpcProfileSnapshot, Error> readRpcProfileSnapshot(const ProfileLease& lease,
    std::string originalPluginDirectory, std::stop_token stop) {
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  auto binding = originalPluginBinding(std::string(runtimeId), std::move(originalPluginDirectory));
  if (!binding) { return std::unexpected(format(binding.error().message)); }
  auto source = readLibrarySource(lease);
  if (!source) { return std::unexpected(source.error()); }
  RpcProfileSnapshot snapshot{.library=std::move(source->view), .binding=std::move(*binding),
      .nextId={}, .counterWrongTypes={}, .identities={}, .issues={}, .legacyInventory=source->legacyInventory};
  auto config = ConfigFile::load(lease, "biglybt.config");
  if (config) {
    OriginalPluginSettings settings(config->document(), snapshot.binding);
    snapshot.nextId = settings.longInteger("xmui.next.id", 1);
    snapshot.counterWrongTypes = settings.wrongTypes();
    snapshot.hideArchived = settings.boolean("xmwebui.hidearchived", false);
  } else if (config.error().code == ErrorCode::Missing) { snapshot.nextId = 1; }
  else { snapshot.issues.push_back({RpcProfileIssue::Scope::Counter, 0, config.error()}); }
  std::map<std::string, const core::Node*, std::less<>> cached;
  if (source->cacheDocument) {
    const auto* entries = source->cacheDocument->document().find("state");
    if (entries && entries->type == core::Node::Type::List) {
      for (const auto& entry : entries->children) {
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        const auto* hash = entry.find("hash");
        if (!hash || hash->type != core::Node::Type::Bytes) { continue; }
        const auto identity = core::TorrentHash::parse(hash->bytes);
        if (identity) { cached.insert_or_assign(identity->hex(), &entry); }
      }
    }
  }
  std::optional<TorrentStateStore> states;
  std::optional<Error> stateOpenError;
  snapshot.identities.reserve(snapshot.library.items.size());
  for (const auto& item : snapshot.library.items) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    PersistedRpcIdentity result{false, item.saved.sourceIndex, item.saved.hash, {}, RpcIdOrigin::Unavailable, {}};
    std::optional<Error> failure;
    if (!result.hash) { failure = format("Original download has no usable saved identity"); }
    else {
      const auto found = cached.find(result.hash->hex());
      const core::Node* attributes = found != cached.end() ? found->second->find("attributes") : nullptr;
      if (attributes) {
        // No primary file read or validation on the authoritative cache path.
        result.addedMillis = originalAddedMillis(attributes);
        const auto value = readOriginalPluginLongMap(attributes, runtimeId, attribute);
        if (value) { result.id = *value; result.origin = RpcIdOrigin::Cache; }
        else { failure = format("Original cached RPC attribute has an invalid type"); }
      } else {
        if (!states && !stateOpenError) {
          auto store = TorrentStateStore::open(lease);
          if (store) { states.emplace(std::move(*store)); }
          else { stateOpenError = store.error(); }
        }
        if (stateOpenError) { failure = *stateOpenError; }
        else {
          ++snapshot.primaryStateLoads;
          const auto state = states->load(lease, *result.hash);
          if (!state) { failure = state.error(); }
          else {
            result.addedMillis = originalAddedMillis(state->document().find("attributes"));
            const auto value = readOriginalPluginLongAttribute(state->document(), nullptr, runtimeId, attribute);
            if (value) { result.id = value->value; result.origin = state->recoveredBackup() ? RpcIdOrigin::Backup : RpcIdOrigin::Primary; }
            else { failure = format("Original primary RPC attribute has an invalid type"); }
          }
        }
      }
    }
    if (failure) { snapshot.issues.push_back({RpcProfileIssue::Scope::Active, result.sourceIndex, std::move(*failure)}); }
    snapshot.identities.push_back(std::move(result));
  }
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  auto archive = readDownloadArchive(lease, stop);
  if (!archive) {
    if (archive.error().code == ErrorCode::Cancelled) { return std::unexpected(cancelled()); }
    if (archive.error().code == ErrorCode::Unsupported) {
      return std::unexpected(format("Original RPC identity snapshot exceeds its torrent limit"));
    }
    snapshot.issues.push_back({RpcProfileIssue::Scope::Archive, 0, archive.error()});
    return snapshot;
  }
  if (archive->stubs.empty()) { return snapshot; }
  if (snapshot.identities.size() > 100000 || archive->stubs.size() > 100000 - snapshot.identities.size()) {
    return std::unexpected(format("Original RPC identity snapshot exceeds its torrent limit"));
  }
  const auto& stubs = archive->source->document().find("stubs")->children;
  for (std::size_t i = 0; i < stubs.size(); ++i) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto& stub = stubs[i];
    auto& archived = archive->stubs[i];
    PersistedRpcIdentity result{true, i, std::move(archived.hash), {}, RpcIdOrigin::Unavailable, {}};
    if (result.hash) {
      result.addedMillis = archived.metadata.archivedMillis;
      result.archive = std::move(archived.metadata);
    }
    const auto value = stub.type == core::Node::Type::Dictionary
        ? readOriginalPluginLongMap(stub.find("attr"), runtimeId, attribute)
        : std::expected<std::int64_t, AttributeError>(std::unexpected(AttributeError::Root));
    if (value && result.hash) { result.id = *value; result.origin = RpcIdOrigin::Archive; }
    else { snapshot.issues.push_back({RpcProfileIssue::Scope::Archive, i, format("Original archive RPC identity is invalid")}); }
    snapshot.identities.push_back(std::move(result));
  }
  return snapshot;
}
}
