// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDateTime>
#include <QLocale>

namespace biglybtc::media {

inline QByteArray mediaHttpDate() {
  return QLocale(QLocale::English).toString(QDateTime::currentDateTimeUtc(),
      QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'")).toLatin1();
}

} // namespace biglybtc::media
