#include "effective_rate_limits.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>

#include <cstdint>
#include <limits>

namespace biglybtc::engine::detail {
static_assert(std::numeric_limits<int>::max() == std::numeric_limits<std::int32_t>::max());
core::SessionRateLimits projectEffectiveRateLimits(const libtorrent::settings_pack& settings) noexcept {
  const auto project = [&settings](int key) {
    return core::effectiveSessionRateLimit(settings.has_val(key)
        ? std::optional<std::int32_t>{settings.get_int(key)} : std::nullopt);
  };
  using Settings = libtorrent::settings_pack;
  const auto integer = [&settings](int key) -> std::optional<std::int32_t> {
    return settings.has_val(key) ? std::optional<std::int32_t>{settings.get_int(key)} : std::nullopt;
  };
  std::optional<bool> automatic;
  if (const auto algorithm = integer(Settings::choking_algorithm)) {
    if (*algorithm == Settings::fixed_slots_choker) { automatic = false; }
    else if (*algorithm == Settings::rate_based_choker) { automatic = true; }
  }
  core::SessionConnectionSettings connections;
  connections.connectionLimit = core::effectiveSessionConnectionLimit(integer(Settings::connections_limit));
  connections.uploadSlots = core::effectiveSessionUploadSlots(integer(Settings::unchoke_slots_limit), automatic);
  if (const auto proxy = integer(Settings::proxy_type)) {
    switch (*proxy) {
      case Settings::none: connections.proxy = core::SessionProxyMode::Disabled; break;
      case Settings::socks4: connections.proxy = core::SessionProxyMode::Socks4; break;
      case Settings::socks5: case Settings::socks5_pw: connections.proxy = core::SessionProxyMode::Socks5; break;
      case Settings::http: case Settings::http_pw: connections.proxy = core::SessionProxyMode::Http; break;
      default: break;
    }
  }
  return {project(Settings::download_rate_limit), project(Settings::upload_rate_limit), connections};
}
core::SessionRateLimits readEffectiveRateLimits(const libtorrent::session& session) noexcept {
  try { return projectEffectiveRateLimits(session.get_settings()); }
  catch (...) { return {}; }
}
} // namespace biglybtc::engine::detail
