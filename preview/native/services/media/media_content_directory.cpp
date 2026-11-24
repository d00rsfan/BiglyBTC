// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_content_directory.hpp"

#include <QUrl>
#include <QXmlStreamWriter>
#include <algorithm>
#include <limits>
#include <vector>

namespace biglybtc::media {
namespace {
constexpr std::uint32_t root = 0;
constexpr std::uint32_t music = 1;
constexpr std::uint32_t pictures = 2;
constexpr std::uint32_t movies = 3;
constexpr std::uint32_t downloads = 4;
constexpr std::uint32_t maxPage = 128;

std::uint32_t mediaContainer(MediaKind kind) {
  switch (kind) {
  case MediaKind::Audio: return music;
  case MediaKind::Picture: return pictures;
  case MediaKind::Video: return movies;
  case MediaKind::Unknown: return downloads;
  }
  return downloads;
}

QString folderTitle(std::uint32_t id) {
  switch (id) {
  case root: return QStringLiteral("BiglyBT");
  case music: return QStringLiteral("Music");
  case pictures: return QStringLiteral("Pictures");
  case movies: return QStringLiteral("Movies");
  default: return QStringLiteral("Downloads");
  }
}

QString text(std::string_view s) {
  return QString::fromLatin1(s.data(), static_cast<qsizetype>(s.size()));
}

QString cleanTitle(const QString& title) {
  QString result;
  result.reserve(title.size());
  for (const auto ch : title) {
    const auto value = ch.unicode();
    if ((value >= 32 && value < 0xfffe) || value == 9 || value == 10 || value == 13) {
      result.append(ch);
    }
  }
  return result;
}

int naturalCompare(QStringView a, QStringView b) {
  qsizetype i = 0;
  qsizetype j = 0;
  while (i < a.size() && j < b.size()) {
    const auto left = a[i++];
    const auto right = b[j++];
    if (!left.isDigit() || !right.isDigit()) {
      const auto difference = left.toLower().unicode() - right.toLower().unicode();
      if (difference != 0) {
        return difference;
      }
      continue;
    }
    const auto startA = i - 1;
    const auto startB = j - 1;
    while (i < a.size() && a[i].isDigit()) {
      ++i;
    }
    while (j < b.size() && b[j].isDigit()) {
      ++j;
    }
    if (i - startA != j - startB) {
      return i - startA < j - startB ? -1 : 1;
    }
    const auto order = a.sliced(startA, i - startA).compare(b.sliced(startB, j - startB));
    if (order != 0) {
      return order;
    }
  }
  if (a.size() == b.size()) {
    return 0;
  }
  return a.size() < b.size() ? -1 : 1;
}

void resource(QXmlStreamWriter& xml, std::string_view type, std::uint64_t bytes, const QByteArray& url, bool growing) {
  xml.writeStartElement("res");
  xml.writeAttribute("protocolInfo", QStringLiteral("http-get:*:") + text(type) + QStringLiteral(":*"));
  if (!growing) { xml.writeAttribute("size", QString::number(bytes)); }
  xml.writeCharacters(QString::fromLatin1(url));
  xml.writeEndElement();
}

void writeItem(QXmlStreamWriter& xml, const MediaDirectoryItem& item, std::uint32_t parent,
    std::uint16_t port, bool xbox, QByteArrayView host) {
  xml.writeStartElement("item");
  xml.writeAttribute("id", QString::number(item.objectId + (parent != downloads)));
  xml.writeAttribute("parentID", QString::number(parent));
  xml.writeAttribute("restricted", "1");
  xml.writeTextElement("dc:title", cleanTitle(item.title));
  xml.writeTextElement("upnp:class", text(item.type.objectClass(xbox)));
  const auto url = mediaResourceUrl(item, port, host);
  resource(xml, item.type.primary, item.bytes, url, item.growing);
  if (!item.type.alternative.empty()) {
    resource(xml, item.type.alternative, item.bytes, url, item.growing);
  }
  xml.writeEndElement();
}

void writeFolder(QXmlStreamWriter& xml, std::uint32_t id, std::span<const MediaDirectoryItem> items) {
  std::uint64_t size = 0;
  auto count = id == root ? 4u : 0u;
  for (const auto& item : items) {
    if (id == downloads || id == root || id == mediaContainer(item.type.kind)) {
      count += id != root;
      size += std::min(item.bytes, std::numeric_limits<std::uint64_t>::max() - size);
    }
  }
  xml.writeStartElement("container");
  xml.writeAttribute("id", QString::number(id));
  xml.writeAttribute("parentID", id == root ? QStringLiteral("-1") : QStringLiteral("0"));
  xml.writeAttribute("childCount", QString::number(count));
  xml.writeAttribute("restricted", "1");
  xml.writeTextElement("dc:title", folderTitle(id));
  xml.writeTextElement("upnp:class", "object.container.storageFolder");
  xml.writeTextElement("upnp:storageUsed", QString::number(size));
  xml.writeEndElement();
}
} // namespace

QByteArray mediaResourceUrl(QByteArrayView hash, std::size_t index, QStringView name,
    std::uint16_t port, QByteArrayView host) {
  QByteArray url("http://");
  url.append(host);
  url += ':' + QByteArray::number(port) + "/Content/";
  url.append(hash);
  url += '-' + QByteArray::number(index);
  const auto dot = name.lastIndexOf(u'.');
  if (dot >= 0 && dot + 1 < name.size()) {
    const auto extension = name.sliced(dot + 1);
    if (std::ranges::none_of(extension, [](QChar ch) {
          const auto value = ch.unicode();
          return value < 32 || value == 127 || ch == u'/' || ch == u'\\' || ch == u'?' || ch == u'#';
        })) {
      url += '.' + QUrl::toPercentEncoding(extension.toString());
    }
  }
  return url;
}

QByteArray mediaResourceUrl(const MediaDirectoryItem& item, std::uint16_t port, QByteArrayView host) {
  if (item.resourcePath.isEmpty()) {
    return mediaResourceUrl(item.torrentHash, item.fileIndex, item.title, port, host);
  }
  return "http://" + host.toByteArray() + ':' + QByteArray::number(port) + item.resourcePath;
}

MediaBrowsePage browseMediaDirectory(std::span<const MediaDirectoryItem> items, std::uint16_t port,
    std::uint32_t updateId, const MediaBrowseRequest& request, QByteArrayView host) {
  MediaBrowsePage page;
  page.updateId = updateId;
  QXmlStreamWriter xml(&page.didl);
  xml.writeStartElement("DIDL-Lite");
  xml.writeDefaultNamespace("urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/");
  xml.writeNamespace("http://purl.org/dc/elements/1.1/", "dc");
  xml.writeNamespace("urn:schemas-upnp-org:metadata-1-0/upnp/", "upnp");

  const auto id = request.objectId;
  const auto count = request.requestedCount == 0 ? maxPage : std::min(request.requestedCount, maxPage);
  if (request.mode == MediaBrowseMode::Metadata) {
    if (id <= downloads) {
      writeFolder(xml, id, items);
      page.numberReturned = page.totalMatches = 1;
    } else {
      for (const auto& item : items) {
        const auto category = mediaContainer(item.type.kind);
        if (item.objectId == id || (category != downloads && item.objectId + 1 == id)) {
          writeItem(xml, item, item.objectId == id ? downloads : category, port, request.xbox, host);
          page.numberReturned = page.totalMatches = 1;
          break;
        }
      }
    }
  } else if (id == root) {
    constexpr std::uint32_t folders[]{downloads, movies, music, pictures};
    page.totalMatches = 4;
    for (auto index = request.startingIndex; index < page.totalMatches && page.numberReturned < count; ++index) {
      writeFolder(xml, folders[index], items);
      ++page.numberReturned;
    }
  } else if (id <= downloads) {
    std::vector<const MediaDirectoryItem*> children;
    for (const auto& item : items) {
      if (id == downloads || id == mediaContainer(item.type.kind)) {
        children.push_back(&item);
      }
    }
    std::ranges::sort(children, [](const auto* a, const auto* b) {
      const auto order = naturalCompare(a->title, b->title);
      return order == 0 ? a->objectId < b->objectId : order < 0;
    });
    page.totalMatches = static_cast<std::uint32_t>(children.size());
    for (auto index = request.startingIndex; index < page.totalMatches && page.numberReturned < count; ++index) {
      writeItem(xml, *children[index], id, port, request.xbox, host);
      ++page.numberReturned;
    }
  }
  xml.writeEndElement();
  return page;
}

} // namespace biglybtc::media
