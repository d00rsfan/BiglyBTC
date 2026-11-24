// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_catalogue.hpp"

namespace biglybtc::update {

enum class PluginBundleError { AssetIdentity, Format, UnexpectedEntry, ModuleIdentity };

class VerifiedPluginBundle final {
public:
  const QByteArray& module() const { return module_; }
  const QByteArray& manifest() const { return manifest_; }
  const VerifiedPluginRelease& release() const { return release_; }

private:
  VerifiedPluginBundle(QByteArray module, QByteArray manifest, VerifiedPluginRelease release)
      : module_(std::move(module)), manifest_(std::move(manifest)), release_(std::move(release)) {}
  friend std::expected<VerifiedPluginBundle, PluginBundleError> readPluginReleaseBundle(
      QIODevice&, const VerifiedPluginRelease&);

  QByteArray module_;
  QByteArray manifest_;
  VerifiedPluginRelease release_;
};

// Consume one intact signed tar.zst asset. Only the native manifest and module
// are accepted. The archive parser never writes, loads, or executes a member.
std::expected<VerifiedPluginBundle, PluginBundleError> readPluginReleaseBundle(
    QIODevice&, const VerifiedPluginRelease&);

} // namespace biglybtc::update
