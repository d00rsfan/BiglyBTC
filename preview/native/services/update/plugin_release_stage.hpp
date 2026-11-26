// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_catalogue.hpp"
#include "original_plugin_inventory.hpp"
#include <stop_token>
#include <vector>

namespace biglybtc::update {

enum class PluginReleaseStage { Published, AlreadyPresent };

// Resolve a unique original installation in the held profile. Saved keys alone
// do not establish an installation, and ambiguous or unsafe paths fail closed.
std::expected<std::optional<std::string>, storage::Error> findInstalledJavaPluginDirectory(
    const storage::ProfileLease&, const PluginReleaseTarget&);

// Stage an intact bundle for one explicitly selected installed Java directory.
// The original configuration and plugin files are read through the held lease.
// The only new artifact is a pending bundle; loading and activation are separate.
std::expected<PluginReleaseStage, storage::Error> stagePluginRelease(
    const storage::ProfileLease&, std::string originalDirectory,
    const VerifiedPluginRelease&, QIODevice& artifact);

// Reload explicitly selected staged data after restart using the installation
// owner's public key and committed sequence. The staged bytes are reverified.
std::expected<VerifiedPluginRelease, storage::Error> readStagedPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QStringView version, qint64 sequence, QByteArrayView approvedPublicKey, qint64 lastSequence);

// Select and authenticate the highest pending sequence newer than the last
// accepted release. Asset-only slots can be left by an interrupted publisher.
std::expected<std::optional<VerifiedPluginRelease>, storage::Error> findStagedPluginRelease(
    const storage::ProfileLease&, std::string originalDirectory, const PluginReleaseTarget&,
    QByteArrayView approvedPublicKey, qint64 lastSequence);

} // namespace biglybtc::update
