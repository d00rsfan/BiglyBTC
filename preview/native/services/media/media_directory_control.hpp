// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>

namespace biglybtc::media {

class MediaContentServer;

inline constexpr qsizetype maximumMediaControlBody = 16 * 1024;

enum class MediaControlService { ContentDirectory, ConnectionManager };

struct MediaControlReply {
  unsigned status;
  QByteArray xml;
};

MediaControlReply controlMediaDevice(MediaContentServer&, MediaControlService, QByteArrayView action,
    QByteArrayView body, bool xbox);

} // namespace biglybtc::media
