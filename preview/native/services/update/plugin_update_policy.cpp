// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_update_policy.hpp"
#include "web_proxy_policy.hpp"

#include <bit>
#include <cstdint>

namespace biglybtc::update {
namespace {
std::expected<bool, PluginUpdatePolicyError> originalBoolean(
    const core::Document* config, const char* key, bool fallback) {
  const auto* value = config ? config->find(key) : nullptr;
  if (!value) { return fallback; }
  if (value->type != core::Node::Type::Integer) { return std::unexpected(PluginUpdatePolicyError{key}); }
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value->integer)) != 0;
}
} // namespace

std::expected<PluginUpdatePlan, PluginUpdatePolicyError> planPluginUpdate(
    const core::Document* config, UpdateCheckTrigger trigger) {
  if (config && config->root().type != core::Node::Type::Dictionary) {
    return std::unexpected(PluginUpdatePolicyError{"biglybt.config"});
  }
  const auto startup = originalBoolean(config, "update.start", true);
  if (!startup) { return std::unexpected(startup.error()); }
  const auto periodic = originalBoolean(config, "update.periodic", true);
  if (!periodic) { return std::unexpected(periodic.error()); }
  const auto automatic = originalBoolean(config, "update.autodownload", false);
  if (!automatic) { return std::unexpected(automatic.error()); }

  using Route = UpdateNetworkRoute;
  if ((trigger == UpdateCheckTrigger::Startup && !*startup && !*periodic) ||
      (trigger == UpdateCheckTrigger::Periodic && !*periodic)) {
    return PluginUpdatePlan{Route::Skipped, *automatic};
  }
  const auto anonymous = originalBoolean(config, "update.anonymous", false);
  if (!anonymous) { return std::unexpected(anonymous.error()); }
  auto proxy = engine::readOriginalWebProxy(config);
  if (!proxy) { return std::unexpected(PluginUpdatePolicyError{std::move(proxy.error().field)}); }
  if (*anonymous) {
    if (*proxy && (*proxy)->kind == engine::NetworkProxyKind::Socks5) {
      const auto disabled = originalBoolean(config, "Proxy.SOCKS.disable.plugin.proxies", true);
      if (!disabled) { return std::unexpected(disabled.error()); }
      if (*disabled) { return PluginUpdatePlan{Route::AnonymousBlocked, *automatic}; }
    }
    return PluginUpdatePlan{Route::NeedsAnonymousProvider, *automatic};
  }
  if (*proxy) { return PluginUpdatePlan{Route::WebProxy, *automatic, std::move(*proxy)}; }
  return PluginUpdatePlan{Route::Direct, *automatic};
}

} // namespace biglybtc::update
