// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_original_type.hpp"

#include <algorithm>
#include <QByteArray>
#include <QString>
#include <set>

namespace biglybtc::media {
namespace {
struct Mapping {
  std::string_view extension;
  OriginalMediaType type;
};

// The original content directory publishes the first MIME type along with
// additional protocol options for the same item when a second type exists.
constexpr Mapping mappings[]{
  {"aif", {MediaKind::Audio, "audio/x-aiff", ""}},
  {"aifc", {MediaKind::Audio, "audio/x-aiff", ""}},
  {"aiff", {MediaKind::Audio, "audio/x-aiff", ""}},
  {"asf", {MediaKind::Video, "video/x-ms-asf", ""}},
  {"asx", {MediaKind::Video, "video/x-ms-asf", ""}},
  {"au", {MediaKind::Audio, "audio/basic", ""}},
  {"avi", {MediaKind::Video, "video/avi", ""}},
  {"bmp", {MediaKind::Picture, "image/bmp", ""}},
  {"cmx", {MediaKind::Picture, "image/x-cmx", ""}},
  {"cod", {MediaKind::Picture, "image/cis-cod", ""}},
  {"flac", {MediaKind::Audio, "audio/flac", ""}},
  {"flv", {MediaKind::Video, "video/x-flv", ""}},
  {"gif", {MediaKind::Picture, "image/gif", ""}},
  {"ico", {MediaKind::Picture, "image/x-icon", ""}},
  {"ief", {MediaKind::Picture, "image/ief", ""}},
  {"jfif", {MediaKind::Picture, "image/pipeg", ""}},
  {"jpe", {MediaKind::Picture, "image/jpeg", ""}},
  {"jpeg", {MediaKind::Picture, "image/jpeg", ""}},
  {"jpg", {MediaKind::Picture, "image/jpeg", ""}},
  {"lsf", {MediaKind::Video, "video/x-la-asf", ""}},
  {"lsx", {MediaKind::Video, "video/x-la-asf", ""}},
  {"m2ts", {MediaKind::Video, "video/MP2T", ""}},
  {"m3u", {MediaKind::Audio, "audio/x-mpegurl", ""}},
  {"m4a", {MediaKind::Audio, "audio/mp4", ""}},
  {"m4v", {MediaKind::Video, "video/m4v", "video/mp4"}},
  {"mid", {MediaKind::Audio, "audio/mid", ""}},
  {"mka", {MediaKind::Audio, "audio/x-matroska", ""}},
  {"mkv", {MediaKind::Video, "video/x-matroska", "video/x-mkv"}},
  {"mov", {MediaKind::Video, "video/quicktime", ""}},
  {"movie", {MediaKind::Video, "video/x-sgi-movie", ""}},
  {"mp2", {MediaKind::Video, "video/mpeg", ""}},
  {"mp3", {MediaKind::Audio, "audio/mpeg", ""}},
  {"mp4", {MediaKind::Video, "video/mp4", ""}},
  {"mpa", {MediaKind::Video, "video/mpeg", ""}},
  {"mpe", {MediaKind::Video, "video/mpeg", ""}},
  {"mpeg", {MediaKind::Video, "video/mpeg", ""}},
  {"mpg", {MediaKind::Video, "video/mpeg", ""}},
  {"mpg4", {MediaKind::Video, "video/mp4", ""}},
  {"mpv2", {MediaKind::Video, "video/mpeg", ""}},
  {"mts", {MediaKind::Video, "video/MP2T", ""}},
  {"nsc", {MediaKind::Video, "video/x-ms-asf", ""}},
  {"ogg", {MediaKind::Audio, "application/ogg", ""}},
  {"ogm", {MediaKind::Video, "application/ogg", ""}},
  {"pbm", {MediaKind::Picture, "image/x-portable-bitmap", ""}},
  {"pgm", {MediaKind::Picture, "image/x-portable-graymap", ""}},
  {"png", {MediaKind::Picture, "image/png", ""}},
  {"pnm", {MediaKind::Picture, "image/x-portable-anymap", ""}},
  {"ppm", {MediaKind::Picture, "image/x-portable-pixmap", ""}},
  {"qt", {MediaKind::Video, "video/quicktime", ""}},
  {"ra", {MediaKind::Audio, "audio/x-pn-realaudio", ""}},
  {"ram", {MediaKind::Audio, "audio/x-pn-realaudio", ""}},
  {"ras", {MediaKind::Picture, "image/x-cmu-raster", ""}},
  {"rgb", {MediaKind::Picture, "image/x-rgb", ""}},
  {"rmi", {MediaKind::Audio, "audio/mid", ""}},
  {"snd", {MediaKind::Audio, "audio/basic", ""}},
  {"tif", {MediaKind::Picture, "image/tiff", ""}},
  {"tiff", {MediaKind::Picture, "image/tiff", ""}},
  {"ts", {MediaKind::Video, "video/MP2T", ""}},
  {"vob", {MediaKind::Video, "video/mpeg", ""}},
  {"wav", {MediaKind::Audio, "audio/x-wav", ""}},
  {"wax", {MediaKind::Audio, "audio/x-ms-wax", ""}},
  {"wm", {MediaKind::Video, "video/x-ms-wm", ""}},
  {"wma", {MediaKind::Audio, "audio/x-ms-wma", ""}},
  {"wmv", {MediaKind::Video, "video/x-ms-wmv", ""}},
  {"wmx", {MediaKind::Video, "video/x-ms-wmx", ""}},
  {"wvx", {MediaKind::Video, "video/x-ms-wvx", ""}},
  {"xbm", {MediaKind::Picture, "image/x-xbitmap", ""}},
  {"xpm", {MediaKind::Picture, "image/x-xpixmap", ""}},
  {"xwd", {MediaKind::Picture, "image/x-xwindowdump", ""}},
};
} // namespace

std::string_view OriginalMediaType::objectClass(bool xbox) const {
  switch (kind) {
  case MediaKind::Audio:
    return "object.item.audioItem.musicTrack";
  case MediaKind::Picture:
    return "object.item.imageItem.photo";
  case MediaKind::Video:
    return xbox ? "object.item.videoItem" : "object.item.videoItem.movie";
  case MediaKind::Unknown:
    return "object.item";
  }
  return "object.item";
}

OriginalMediaType originalMediaType(QStringView fileName) {
  constexpr OriginalMediaType unknown{MediaKind::Unknown, "unknown/unknown", {}};
  const auto separator = fileName.lastIndexOf(u'/');
  const auto dot = fileName.lastIndexOf(u'.');
  if (dot < 0 || dot < separator || dot == fileName.size() - 1) {
    return unknown;
  }
  const QByteArray extension = fileName.sliced(dot + 1).toString().toLower().toLatin1();
  const std::string_view key(extension.constData(), static_cast<std::size_t>(extension.size()));
  const auto entry = std::ranges::lower_bound(mappings, key, {}, &Mapping::extension);
  return entry != std::end(mappings) && entry->extension == key ? entry->type : unknown;
}

QByteArray mediaSourceProtocols() {
  std::set<std::string_view> used{"application/octet-stream"};
  for (const auto& mapping : mappings) {
    used.insert(mapping.type.primary);
    if (!mapping.type.alternative.empty()) {
      used.insert(mapping.type.alternative);
    }
  }
  QByteArray protocols;
  for (const auto mime : used) {
    if (!protocols.isEmpty()) {
      protocols += ',';
    }
    protocols += "http-get:*:";
    protocols.append(mime.data(), static_cast<qsizetype>(mime.size()));
    protocols += ":*";
  }
  return protocols;
}

} // namespace biglybtc::media
