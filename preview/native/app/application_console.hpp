// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/console_history.hpp"

#include <QByteArray>
#include <QString>
#include <QtCore/qlogging.h>
#include <cstdio>

namespace biglybtc::app::detail {

inline QtMessageHandler previousConsoleHandler = nullptr;

inline void applicationConsoleHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  try {
    const auto encoded = message.left(4096).toUtf8();
    core::applicationConsole().append(std::string_view(encoded.constData(), static_cast<std::size_t>(encoded.size())));
  } catch (...) {}
  if (previousConsoleHandler) { previousConsoleHandler(type, context, message); }
  else {
    const auto formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", formatted.constData());
  }
}

inline void installApplicationConsole() {
  previousConsoleHandler = qInstallMessageHandler(applicationConsoleHandler);
}

} // namespace biglybtc::app::detail
