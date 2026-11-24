// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_trial.hpp"
#include "profile.hpp"

namespace biglybtc::update {

enum class PluginSlotPublication { Published, AlreadyPresent };

// Read and authenticate the pending bundle of the same installed Java plugin,
// trial it in the dedicated child and publish an immutable native version.
// This step keeps the pending bundle for receipt/recovery and selects no
// active plugin version.
std::expected<PluginSlotPublication, storage::Error> publishPluginReleaseSlot(
    const storage::ProfileLease&, std::string originalDirectory, const VerifiedPluginRelease&,
    const PluginTrialProgram&, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token = {});

} // namespace biglybtc::update
