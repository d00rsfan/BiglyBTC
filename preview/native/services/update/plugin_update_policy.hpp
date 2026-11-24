// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "network_policy.hpp"

#include <expected>
#include <optional>
#include <string>

namespace biglybtc::update {

enum class UpdateCheckTrigger { Startup, Periodic, Explicit };
enum class UpdateNetworkRoute { Skipped, Direct, WebProxy, NeedsAnonymousProvider, AnonymousBlocked };

struct PluginUpdatePlan {
  UpdateNetworkRoute route;
  bool downloadAutomatically;
  std::optional<engine::NetworkProxy> proxy{};
};

struct PluginUpdatePolicyError {
  // Configuration key only. Values and proxy credentials stay private.
  std::string field;
};

// Computes the original saved preferences before any connection is opened.
// The selected route is explicit, including a required anonymous provider.
std::expected<PluginUpdatePlan, PluginUpdatePolicyError> planPluginUpdate(
    const core::Document*, UpdateCheckTrigger);

} // namespace biglybtc::update
