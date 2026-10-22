#pragma once

#include "biglybtc/effective_rate_limits.hpp"

#include <QCoreApplication>
#include <QString>

namespace biglybtc::ui {
inline QString effectiveRateLimitText(const core::EffectiveRateLimit& value) {
  if (!core::validEffectiveRateLimit(value) || value.state == core::RateLimitState::Unavailable) {
    return QCoreApplication::translate("EffectiveRateLimit", "Unavailable");
  }
  if (value.state == core::RateLimitState::Unlimited) {
    return QCoreApplication::translate("EffectiveRateLimit", "Unlimited");
  }
  return QCoreApplication::translate("EffectiveRateLimit", "%1 bytes/s").arg(value.bytesPerSecond);
}
inline QString effectiveUploadSlotsText(const core::EffectiveUploadSlots& value) {
  const auto unavailable = [] { return QCoreApplication::translate("EffectiveRateLimit", "Unavailable"); };
  if (value.state == core::UploadSlotState::Limited) {
    if (value.count > 0 && value.count < std::numeric_limits<std::int32_t>::max()) { return QString::number(value.count); }
    return unavailable();
  }
  if (value.count != 0) { return unavailable(); }
  switch (value.state) {
    case core::UploadSlotState::Disabled: return QCoreApplication::translate("EffectiveRateLimit", "Disabled (0 fixed slots)");
    case core::UploadSlotState::Unlimited: return QCoreApplication::translate("EffectiveRateLimit", "Unlimited");
    case core::UploadSlotState::Automatic: return QCoreApplication::translate("EffectiveRateLimit", "Automatic (rate-based)");
    case core::UploadSlotState::Unavailable: case core::UploadSlotState::Limited: return unavailable();
  }
  return unavailable();
}
inline QString sessionProxyModeText(core::SessionProxyMode mode) {
  switch (mode) {
    case core::SessionProxyMode::Disabled: return QCoreApplication::translate("EffectiveRateLimit", "Disabled");
    case core::SessionProxyMode::Socks4: return QCoreApplication::translate("EffectiveRateLimit", "Enabled (SOCKS4)");
    case core::SessionProxyMode::Socks5: return QCoreApplication::translate("EffectiveRateLimit", "Enabled (SOCKS5)");
    case core::SessionProxyMode::Http: return QCoreApplication::translate("EffectiveRateLimit", "Enabled (HTTP)");
    case core::SessionProxyMode::Unavailable: break;
  }
  return QCoreApplication::translate("EffectiveRateLimit", "Unavailable");
}
inline QString sessionConnectionSettingsText(const core::SessionConnectionSettings& value) {
  const auto limit = value.connectionLimit && *value.connectionLimit > 0 ? QString::number(*value.connectionLimit)
      : QCoreApplication::translate("EffectiveRateLimit", "Unavailable");
  return QCoreApplication::translate("EffectiveRateLimit", "Connections %1; upload slots %2; proxy %3")
      .arg(limit).arg(effectiveUploadSlotsText(value.uploadSlots)).arg(sessionProxyModeText(value.proxy));
}
inline QString sessionConnectionSettingsScope() {
  return QCoreApplication::translate("EffectiveRateLimit",
      "Settings observed once after owner creation. Connections are a session-global limit, not current peers; per-torrent floors and incoming slack may exceed it. Upload slots describe choking, not all peer traffic or capacity. Proxy mode does not prove any particular connection used the proxy.");
}
inline QString effectiveRateLimitScope() {
  return QCoreApplication::translate("EffectiveRateLimit",
      "Applied session-global peer-class limit. Local peers may bypass it; torrent and other peer-class limits may also apply. This is not capacity or an application-wide limit.");
}
} // namespace biglybtc::ui
