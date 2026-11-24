// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "file_io.hpp"
#include "plugin_release_catalogue.hpp"
#include "plugin_settings.hpp"

#include <array>
#include <span>
#include <vector>

namespace biglybtc::update::detail {

using storage::detail::Descriptor;

inline constexpr char pendingAsset[] = "release.asset";

struct InstalledJavaPlugin {
  std::shared_ptr<const storage::ProfileLease> lease;
  storage::OriginalPluginBinding binding;
  Descriptor plugins;
  Descriptor original;
  std::string descriptor;
};

std::expected<InstalledJavaPlugin, storage::Error> openInstalledJavaPlugin(
    const storage::ProfileLease&, std::string, const PluginReleaseTarget&);
std::expected<void, storage::Error> ensureCurrent(const InstalledJavaPlugin&);

bool heldAt(int parent, const std::string& leaf, int descriptor);
std::expected<std::vector<std::string>, storage::Error> childNames(int directory, std::size_t limit);
std::expected<Descriptor, storage::Error> preparePluginReceipt(int directory, QByteArrayView bytes);
int linkPluginTemporary(int descriptor, int directory, const char* name);
std::expected<Descriptor, storage::Error> directory(int parent, const std::string& leaf, bool create,
    bool privateDirectory = true);
std::expected<Descriptor, storage::Error> openArtifact(int parent, const char* name);
std::expected<std::vector<Descriptor>, storage::Error> descend(int parent, std::span<const std::string>, bool create);
bool isCurrentPath(int parent, std::span<const std::string>, std::span<const Descriptor>);
std::array<std::string, 5> pendingPath(const PluginReleaseTarget&, QStringView version);
std::array<std::string, 5> pendingPath(const VerifiedPluginRelease&);
std::array<std::string, 3> versionParent(const VerifiedPluginRelease&);

} // namespace biglybtc::update::detail
