// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_disk.hpp"
#include "plugin_release_trial.hpp"

namespace biglybtc::update::detail {

struct CheckedInstalledSlot {
  VerifiedPluginBundle bundle;
  Descriptor descriptor;
};

std::expected<CheckedInstalledSlot, storage::Error> checkedInstalledBundle(
    const InstalledJavaPlugin&, const VerifiedPluginRelease&);

// Only the activation store admits the historical signature before using this
// trial path. New release admission uses the public expiry-checked wrapper.
std::expected<void, PluginTrialError> trialPluginModule(const VerifiedPluginBundle&,
    const PluginTrialProgram&, std::uint64_t capabilities, std::chrono::milliseconds deadline, std::stop_token);

} // namespace biglybtc::update::detail
