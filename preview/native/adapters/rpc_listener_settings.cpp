// SPDX-License-Identifier: GPL-2.0-or-later
#include "rpc_listener_settings.hpp"

#include "application_settings.hpp"
#include "plugin_keys.hpp"
#include "plugin_settings.hpp"

#include <algorithm>

namespace biglybtc::storage {
namespace {
constexpr std::uint16_t originalPort = 9091;

bool foldedEquals(std::string_view value, std::string_view expected) {
  return std::ranges::equal(value, expected, [](char left, char right) {
    const auto lower = left >= 'A' && left <= 'Z' ? static_cast<char>(left - 'A' + 'a') : left;
    return lower == right;
  });
}
} // namespace

std::expected<SavedRpcListener, Error> readSavedRpcListener(const ProfileLease& lease, std::string directory) {
  const auto file = ConfigFile::load(lease, "biglybt.config", ApplicationSettingsLimits);
  if (!file) { return std::unexpected(file.error()); }
  return inspectSavedRpcListener(*file, std::move(directory));
}

std::expected<SavedRpcListener, Error> inspectSavedRpcListener(const ConfigFile& file, std::string directory) {
  const auto binding = originalPluginBinding("xmwebui", std::move(directory));
  if (!binding) { return std::unexpected(Error{ErrorCode::InvalidFormat, binding.error().message}); }
  const auto& document = file.document();
  if (file.recoverySource() != RecoverySource::Primary || document.root().type != core::Node::Type::Dictionary) {
    return std::unexpected(Error{ErrorCode::Conflict, "A recovered or invalid settings file cannot configure RPC authentication"});
  }
  OriginalPluginSettings settings(document, *binding);
  const bool started = settings.loadedAtStartup() && settings.boolean("Enable", true);
  const bool passwordEnabled = settings.boolean("Password Enable", false);
  const auto port = settings.longInteger("Port", originalPort);
  if (!settings.wrongTypes().empty() || port <= 0 || port > 65535) {
    return std::unexpected(Error{ErrorCode::InvalidFormat, "The saved original RPC settings are invalid"});
  }
  if (!started || !passwordEnabled) {
    return std::unexpected(Error{ErrorCode::Unsupported, "The saved RPC listener and password protection must be enabled"});
  }
  const auto value = [&](const std::string& key) { return detail::findOriginalKey(document.root(), binding->configPrefix + key); };
  const auto* protocol = value("Protocol");
  if (protocol && (protocol->type != core::Node::Type::Bytes || !foldedEquals(protocol->bytes, "http"))) {
    return std::unexpected(Error{ErrorCode::Unsupported, "The saved RPC protocol requires a different listener"});
  }
  const auto* user = value("User");
  const auto* password = value("Password");
  if (!user || user->type != core::Node::Type::Bytes || user->bytes.empty() || user->bytes.size() > 128 ||
      !password || password->type != core::Node::Type::Bytes || password->bytes.size() != 20) {
    return std::unexpected(Error{ErrorCode::InvalidFormat, "The saved original RPC username or password is unavailable"});
  }
  const auto* mode = value("Mode");
  const bool full = !mode || (mode->type == core::Node::Type::Bytes && foldedEquals(mode->bytes, "full"));
  return SavedRpcListener{static_cast<std::uint16_t>(port), std::string(user->bytes), std::string(password->bytes), full};
}

} // namespace biglybtc::storage
