// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "file_io.hpp"
#include "plugin_activation_state.hpp"
#include "plugin_release_trial.hpp"
#include "profile.hpp"

namespace biglybtc::update {

struct ActivatedPluginModule {
  PluginActivationEntry identity;
  storage::detail::Descriptor descriptor;
  storage::detail::Descriptor originalDirectory;
  // Keep the actual shared-profile lock alive until the loader binds this fd.
  std::shared_ptr<const storage::ProfileLease> profile;
};

std::expected<PluginActivationState, storage::Error> readPluginInstallation(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey);

std::expected<qint64, storage::Error> readPluginReleaseReplayFloor(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey);

std::expected<std::optional<ActivatedPluginModule>, storage::Error> openActivatedPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey);

std::expected<PluginActivationState, storage::Error> activatePluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const VerifiedPluginRelease&,
    const PluginTrialProgram&, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token = {});

std::expected<PluginActivationState, storage::Error> activateStagedPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&, QStringView version,
    qint64 sequence, QByteArrayView approvedPublicKey, const PluginTrialProgram&, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token = {});

std::expected<std::optional<PluginActivationState>, storage::Error> activateLatestStagedPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey, const PluginTrialProgram&, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token = {});

std::expected<PluginActivationState, storage::Error> rollbackPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey, const PluginTrialProgram&, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token = {});

} // namespace biglybtc::update
