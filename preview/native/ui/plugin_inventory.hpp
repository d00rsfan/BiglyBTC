// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <optional>

namespace biglybtc::ui {

struct JavaPlugin {
  QString runtimeId;
  std::optional<QString> directory;
  std::optional<bool> startup;
  QString installationFailure;
  QString settingsFailure;
};

struct JavaPluginInventory {
  quint64 session = 0;
  QList<JavaPlugin> entries;
  QString failure;
};

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::JavaPluginInventory)
