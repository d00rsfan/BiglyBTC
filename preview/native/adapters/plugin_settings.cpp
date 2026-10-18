// SPDX-License-Identifier: GPL-2.0-or-later
// Original core prefix/numeric semantics: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// Original mlDHT defaults: 3b12515c5541ed75cfb4f795b672d4e786df9f45.
#include "plugin_settings.hpp"
#include "plugin_keys.hpp"
#include <bit>
namespace biglybtc::storage {
std::expected<OriginalPluginBinding, PluginSettingsError> originalPluginBinding(
    std::string runtimeId, std::string directoryName) {
  if (runtimeId.empty() || runtimeId.size() > 64 || runtimeId.find('\0') != std::string::npos
      || directoryName.empty() || directoryName.size() > 255 || directoryName == "." || directoryName == ".."
      || directoryName.find('/') != std::string::npos || directoryName.find('\0') != std::string::npos) {
    return std::unexpected(PluginSettingsError{"Original runtime ID and external directory basename required"});
  }
  auto prefix = "Plugin." + directoryName + '.';
  auto startup = "PluginInfo." + runtimeId + ".enabled";
  return OriginalPluginBinding{std::move(runtimeId), std::move(directoryName), std::move(prefix), std::move(startup)};
}
std::int32_t OriginalPluginSettings::fullInteger(const std::string& key, std::int32_t fallback) {
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(fullLong(key, fallback)));
}
std::int64_t OriginalPluginSettings::fullLong(const std::string& key, std::int64_t fallback) {
  const auto* node = detail::findOriginalKey(document_.root(), key);
  if (!node) { return fallback; }
  if (node->type != core::Node::Type::Integer) { wrongTypes_.push_back(key); return fallback; }
  return node->integer;
}
std::int32_t OriginalPluginSettings::integer(std::string_view name, std::int32_t fallback) {
  return fullInteger(binding_.configPrefix + std::string(name), fallback);
}
std::int64_t OriginalPluginSettings::longInteger(std::string_view name, std::int64_t fallback) {
  return fullLong(binding_.configPrefix + std::string(name), fallback);
}
bool OriginalPluginSettings::boolean(std::string_view name, bool fallback) {
  return integer(name, fallback ? 1 : 0) != 0;
}
bool OriginalPluginSettings::loadedAtStartup() { return fullInteger(binding_.startupKey, 1) != 0; }
std::expected<OriginalMldhtSettings, PluginSettingsError> readOriginalMldhtSettings(
    const core::Document& configuration, std::string originalDirectoryName) {
  if (configuration.root().type != core::Node::Type::Dictionary) {
    return std::unexpected(PluginSettingsError{"Original plugin configuration must be a dictionary"});
  }
  auto binding = originalPluginBinding("mldht", std::move(originalDirectoryName));
  if (!binding) { return std::unexpected(binding.error()); }
  OriginalPluginSettings view(configuration, std::move(*binding));
  OriginalMldhtSettings result{.binding = view.binding()};
  result.loadedAtStartup = view.loadedAtStartup();
  result.enabled = view.boolean("enable", true);
  result.port = view.integer("port", 49001);
  result.autostart4 = view.boolean("autostart.IPv4", true);
  result.autostart6 = view.boolean("autostart.IPv6", true);
  result.autoopen4 = view.boolean("autoopen.IPv4", false);
  result.autoopen6 = view.boolean("autoopen.IPv6", false);
  result.backupOnly = view.boolean("backupOnly", false);
  result.onlyPeerBootstrap = view.boolean("onlyPeerBootstrap", false);
  result.alwaysRestoreId = view.boolean("alwaysRestoreID", true);
  result.showStatusEntry = view.boolean("showStatusEntry", true);
  result.multihoming4 = view.boolean("multihoming", false);
  result.multihoming6 = view.boolean("multihoming6", true);
  result.wrongTypes = view.wrongTypes();
  return result;
}
}
