// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::storage {
struct PluginSettingsError { std::string message; };
// External directory basename is independent from runtime ID and repository name.
struct OriginalPluginBinding {
  std::string runtimeId, directoryName, configPrefix, startupKey;
};
std::expected<OriginalPluginBinding, PluginSettingsError> originalPluginBinding(
    std::string runtimeId, std::string directoryName);
// Owner-thread read-only view; must not outlive the supplied document. No default
// registration, writes or filesystem creation happen while reading a snapshot.
class OriginalPluginSettings final {
public:
  OriginalPluginSettings(const core::Document& document, OriginalPluginBinding binding)
      : document_(document), binding_(std::move(binding)) {}
  std::int32_t integer(std::string_view name, std::int32_t fallback);
  std::int64_t longInteger(std::string_view name, std::int64_t fallback);
  bool boolean(std::string_view name, bool fallback);
  bool loadedAtStartup();
  const std::vector<std::string>& wrongTypes() const { return wrongTypes_; }
  const OriginalPluginBinding& binding() const { return binding_; }
private:
  std::int32_t fullInteger(const std::string& key, std::int32_t fallback);
  std::int64_t fullLong(const std::string& key, std::int64_t fallback);
  const core::Document& document_;
  OriginalPluginBinding binding_;
  std::vector<std::string> wrongTypes_;
};
struct OriginalMldhtSettings {
  OriginalPluginBinding binding;
  std::int32_t port = 49001;
  bool loadedAtStartup = true, enabled = true;
  bool autostart4 = true, autostart6 = true, autoopen4 = false, autoopen6 = false;
  bool backupOnly = false, onlyPeerBootstrap = false, alwaysRestoreId = true;
  bool showStatusEntry = true, multihoming4 = false, multihoming6 = true;
  std::vector<std::string> wrongTypes{};
};
std::expected<OriginalMldhtSettings, PluginSettingsError> readOriginalMldhtSettings(
    const core::Document& configuration, std::string originalDirectoryName);
}
