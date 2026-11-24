// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMap>
#include <QUuid>

namespace biglybtc::ui {

using MediaRenderers = QMap<QUuid, QString>;
enum class MediaPlaybackState { Idle, Preparing, Starting, Playing, Stopping, Stopped, Failed };
enum class MediaRendererEncoding { Original, Mp4, Mp4_480p, Mp4_720p, Mp4Stream };

} // namespace biglybtc::ui
