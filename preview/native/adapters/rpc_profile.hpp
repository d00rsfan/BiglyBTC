// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "library_view.hpp"
#include "plugin_settings.hpp"
#include <stop_token>
namespace biglybtc::storage {
enum class RpcIdOrigin { Unavailable, Cache, Primary, Backup, Archive };
struct PersistedRpcIdentity {
  bool archived;
  std::size_t sourceIndex;
  std::optional<core::TorrentHash> hash;
  std::optional<std::int64_t> id;
  RpcIdOrigin origin = RpcIdOrigin::Unavailable;
};
struct RpcProfileIssue {
  enum class Scope { Counter, Active, Archive } scope;
  std::size_t sourceIndex;
  Error error;
};
struct RpcProfileSnapshot {
  LibraryView library;
  OriginalPluginBinding binding;
  std::optional<std::int64_t> nextId;
  std::vector<std::string> counterWrongTypes;
  std::vector<PersistedRpcIdentity> identities; // Active inventory, then archive file order.
  std::vector<RpcProfileIssue> issues;
  std::size_t primaryStateLoads = 0;
  bool legacyInventory = false;
};
// Borrow the host's existing lease. Read-only persisted values, not a running
// plugin registry: actual runtime registration/magnet membership/order must be
// established before constructing/applying the identity planner or serving RPC.
std::expected<RpcProfileSnapshot, Error> readRpcProfileSnapshot(const ProfileLease& lease,
    std::string originalPluginDirectory, std::stop_token stop = {});
}
