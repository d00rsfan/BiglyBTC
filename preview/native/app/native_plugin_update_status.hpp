// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMetaType>
#include <QString>
#include <optional>
#include <stop_token>

namespace biglybtc::app {

enum class NativePluginAction { Status, Download, Install, Rollback };

struct NativePluginVersion {
  QString version;
  qint64 sequence;
  bool operator==(const NativePluginVersion&) const = default;
};

struct NativePluginRequest {
  quint64 session;
  NativePluginAction action;
  std::optional<NativePluginVersion> selected;
  std::stop_token cancellation{};
};

struct NativePluginStatus {
  quint64 session = 0;
  QString running;
  QString startupFailure;
  QString message;
  bool canDownload = false;
  std::optional<NativePluginVersion> active;
  std::optional<NativePluginVersion> previous;
  std::optional<NativePluginVersion> staged;
};

} // namespace biglybtc::app

Q_DECLARE_METATYPE(biglybtc::app::NativePluginStatus)
