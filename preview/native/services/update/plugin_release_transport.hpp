// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_catalogue.hpp"
#include "plugin_update_policy.hpp"

#include <QSslConfiguration>
#include <QStringList>
#include <chrono>
#include <stop_token>
#include <variant>

namespace biglybtc::update {

struct PluginDownloadOptions {
  // Repository origins and delivery hosts are supplied by the installation owner.
  QStringList deliveryHosts;
  QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
  std::chrono::milliseconds deadline{30000};
  std::stop_token cancellation{};
};

enum class PluginDownloadError { Policy, Expired, Redirect, Deadline, Response, Tls, AssetIdentity, CatalogueSize, Cancelled };
using PluginCatalogueCheckError = std::variant<PluginDownloadError, CatalogueError>;

// Fetches the named release-channel catalogue and its raw Ed25519 signature
// from <name>.sig. Trust and the last accepted sequence come from the owner.
std::expected<VerifiedPluginRelease, PluginCatalogueCheckError> checkPluginReleaseCatalogue(
    const PluginReleaseTarget&, QStringView name, QByteArrayView signingKey, qint64 lastSequence,
    const PluginUpdatePlan&, const PluginDownloadOptions&);

// Synchronous only on the dedicated update worker. The reply body is bounded
// by its signed size, authenticated and returned without touching the profile.
std::expected<QByteArray, PluginDownloadError> downloadPluginReleaseAsset(
    const VerifiedPluginRelease&, const PluginUpdatePlan&, const PluginDownloadOptions&);

} // namespace biglybtc::update
