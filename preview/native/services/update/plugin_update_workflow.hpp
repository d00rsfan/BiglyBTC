// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_stage.hpp"
#include "plugin_release_transport.hpp"

namespace biglybtc::update {

enum class PluginUpdateIntent { AvailabilityOnly, FollowSavedDownload, ExplicitDownload };

struct PluginUpdateOutcome {
  // Empty when the saved schedule skips the check.
  std::optional<VerifiedPluginRelease> release;
  std::optional<PluginReleaseStage> staged;
  bool alreadyAccepted = false;
};

using PluginUpdateError = std::variant<storage::Error, PluginUpdatePolicyError, PluginDownloadError, CatalogueError>;

// Run on the update worker with a held profile lease and an explicitly trusted
// channel. An empty directory selects a unique installed Java plugin. A
// staged release still requires an isolated trial and activation.
std::expected<PluginUpdateOutcome, PluginUpdateError> checkInstalledPluginUpdate(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QStringView catalogueName, QByteArrayView approvedPublicKey, UpdateCheckTrigger,
    PluginUpdateIntent, const PluginDownloadOptions&);

} // namespace biglybtc::update
