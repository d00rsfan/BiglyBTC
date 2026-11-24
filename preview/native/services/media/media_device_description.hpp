// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>

namespace biglybtc::media {

struct MediaDeviceIdentity {
  QUuid id;
  QString name;
  bool valid() const;
  QString udn() const;
};

enum class MediaDeviceDocument { Root, ContentDirectory, ConnectionManager };
QByteArray describeMediaDevice(const MediaDeviceIdentity&, MediaDeviceDocument);

} // namespace biglybtc::media
