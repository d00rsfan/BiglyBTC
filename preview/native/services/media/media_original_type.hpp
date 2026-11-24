// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QStringView>
#include <string_view>

namespace biglybtc::media {

enum class MediaKind { Unknown, Audio, Picture, Video };

struct OriginalMediaType {
  MediaKind kind;
  std::string_view primary;
  std::string_view alternative;

  std::string_view objectClass(bool xbox = false) const;
};

OriginalMediaType originalMediaType(QStringView fileName);
QByteArray mediaSourceProtocols();

} // namespace biglybtc::media
