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
    PersistedRpcIdentity result{false, item.saved.sourceIndex, item.saved.hash, {}, RpcIdOrigin::Unavailable};
    std::optional<Error> failure;
    if (!result.hash) { failure = format("Original download has no usable saved identity"); }
    else {
      const auto found = cached.find(result.hash->hex());
      const core::Node* attributes = found != cached.end() ? found->second->find("attributes") : nullptr;
      if (attributes) {
        // No primary file read or validation on the authoritative cache path.
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
  auto archive = ConfigFile::load(lease, "dlarchive.config");
  if (!archive) {
    if (archive.error().code != ErrorCode::Missing) { snapshot.issues.push_back({RpcProfileIssue::Scope::Archive, 0, archive.error()}); }
  } else {
    const auto* stubs = archive->document().find("stubs");
    if (stubs && stubs->type != core::Node::Type::List) {
      snapshot.issues.push_back({RpcProfileIssue::Scope::Archive, 0, format("Original archive stub list has an invalid type")});
    } else if (stubs) {
      if (snapshot.identities.size() > 100000 || stubs->children.size() > 100000 - snapshot.identities.size()) {
        return std::unexpected(format("Original RPC identity snapshot exceeds its torrent limit"));
      }
      for (std::size_t i = 0; i < stubs->children.size(); ++i) {
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        const auto& stub = stubs->children[i];
        PersistedRpcIdentity result{true, i, {}, {}, RpcIdOrigin::Unavailable};
        const auto* hash = stub.find("hash");
        if (stub.type == core::Node::Type::Dictionary && hash && hash->type == core::Node::Type::Bytes) {
          const auto identity = core::TorrentHash::parse(hash->bytes);
          if (identity) { result.hash = *identity; }
        }
        const auto value = stub.type == core::Node::Type::Dictionary
            ? readOriginalPluginLongMap(stub.find("attr"), runtimeId, attribute)
            : std::expected<std::int64_t, AttributeError>(std::unexpected(AttributeError::Root));
        if (value && result.hash) { result.id = *value; result.origin = RpcIdOrigin::Archive; }
        else { snapshot.issues.push_back({RpcProfileIssue::Scope::Archive, i, format("Original archive RPC identity is invalid")}); }
        snapshot.identities.push_back(std::move(result));
      }
    }
  }
  return snapshot;
}
}
