// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_original_type.hpp"

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <cstdint>
#include <span>

namespace biglybtc::media {

struct MediaDirectoryItem {
  std::uint32_t objectId;
  QString title;
  QByteArray torrentHash;
  std::size_t fileIndex;
  std::uint64_t bytes;
  OriginalMediaType type;
  QByteArray resourcePath{};
  bool growing = false;
};

enum class MediaBrowseMode { Metadata, DirectChildren };

struct MediaBrowseRequest {
  std::uint32_t objectId = 0;
  MediaBrowseMode mode = MediaBrowseMode::Metadata;
  std::uint32_t startingIndex = 0;
  std::uint32_t requestedCount = 0;
  bool xbox = false;
};

struct MediaBrowsePage {
  QByteArray didl;
  std::uint32_t numberReturned = 0;
  std::uint32_t totalMatches = 0;
  std::uint32_t updateId = 0;
};

QByteArray mediaResourceUrl(QByteArrayView hash, std::size_t index, QStringView name,
    std::uint16_t port, QByteArrayView host = "127.0.0.1");

QByteArray mediaResourceUrl(const MediaDirectoryItem&, std::uint16_t port, QByteArrayView host);

// The caller supplies current publications and owns the loopback resource port.
MediaBrowsePage browseMediaDirectory(std::span<const MediaDirectoryItem>, std::uint16_t resourcePort,
    std::uint32_t updateId, const MediaBrowseRequest&, QByteArrayView host = "127.0.0.1");

} // namespace biglybtc::media
