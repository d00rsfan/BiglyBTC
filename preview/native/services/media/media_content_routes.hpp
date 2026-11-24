// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_content_directory.hpp"
#include "media_directory_control.hpp"
#include "media_device_description.hpp"
#include "media_event_subscriptions.hpp"
#include "media_lan_endpoint.hpp"
#include "torrent_http_body.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <map>

namespace biglybtc::media {

class TorrentTranscode;

inline constexpr std::size_t maximumMediaHttpHeader = 8192;

enum class MediaRequestError { BadRequest, Unauthorized, Forbidden, NotFound, Unsupported, RangeRejected };

struct MediaDescriptionRequest {
  MediaDeviceDocument document;
  bool sendBody;
};

struct MediaControlRequest {
  MediaControlService service;
  qsizetype contentLength;
  QByteArray action;
  bool xbox;
};

struct MediaRendererResource {
  QByteArray url;
  QByteArray didl;
  QStringList types;
};

struct MediaBasicCredentials {
  QByteArray username;
  QByteArray passwordSha1;
};

// The engine worker publishes specific retained files. A URL can select one of
// those files, but cannot open a new torrent or select an unlisted file.
class MediaContentRoutes final {
public:
  using Selection = engine::PersistentSession::VerifiedFileSelection;

  explicit MediaContentRoutes(std::weak_ptr<engine::PersistentSession>,
      std::optional<MediaBasicCredentials> credentials = {});
  void configureLan(const MediaLanEndpoint&, std::uint16_t port);
  bool acceptsPeer(const QHostAddress&) const;
  bool publish(const Selection&);
  bool withdraw(const Selection&);
  bool selectRendererType(const QHostAddress& peer, std::string_view path, QStringView type);
  void clearRendererType() { alternateType_.reset(); }
  std::optional<MediaRendererResource> publishTranscode(
      const Selection&, std::shared_ptr<TorrentTranscode>, std::uint16_t port);
  bool withdrawTranscode(const std::shared_ptr<TorrentTranscode>&);
  QByteArray transcodedUrl(const std::shared_ptr<TorrentTranscode>&, std::uint16_t port) const;
  void clearTranscodes();
  QByteArray publishedUrl(const Selection&, std::uint16_t port) const;
  std::optional<MediaRendererResource> rendererResource(const Selection&, std::uint16_t port) const;
  MediaBrowsePage browse(std::uint16_t port, const MediaBrowseRequest&);
  std::expected<MediaControlRequest, MediaRequestError> controlRequest(
      const QHostAddress& peer, std::string_view header) const;
  std::expected<MediaEventRequest, MediaRequestError> eventRequest(
      const QHostAddress& peer, std::string_view header) const;
  std::expected<std::optional<MediaDescriptionRequest>, MediaRequestError> descriptionRequest(
      const QHostAddress& peer, std::string_view header) const;
  std::expected<std::unique_ptr<TorrentHttpBody>, MediaRequestError> resolve(
      const QHostAddress& peer, std::string_view request) const;

private:
  using Key = std::pair<std::string, std::size_t>;
  std::weak_ptr<engine::PersistentSession> session_;
  std::optional<MediaBasicCredentials> credentials_;
  std::optional<MediaLanEndpoint> lan_;
  QByteArray resourceHost_ = "127.0.0.1";
  QByteArray lanAuthority_;
  struct Publication {
    Selection file;
    std::uint32_t objectId;
    QString name;
    OriginalMediaType type;
    QByteArray path{};
    std::shared_ptr<TorrentTranscode> job{};
    std::shared_ptr<QFile> artifact{};
    std::uint64_t bytes = 0;
    bool progressive = false;

    std::optional<MediaDirectoryItem> item(const engine::PersistentSession&) const;
  };
  struct RendererTypeBinding {
    QHostAddress peer;
    std::weak_ptr<const Publication> publication;
  };
  std::expected<std::shared_ptr<const Publication>, MediaRequestError> findPublication(std::string_view target) const;
  std::optional<MediaDirectoryItem> publishedItem(const Selection&) const;
  MediaRendererResource rendererResource(const MediaDirectoryItem&, std::uint16_t port) const;
  std::map<Key, std::shared_ptr<const Publication>> files_;
  std::map<QByteArray, std::shared_ptr<const Publication>> transcodes_;
  std::optional<RendererTypeBinding> alternateType_;
  std::vector<std::uint32_t> lastVisible_;
  std::uint32_t nextObjectId_ = 5;
  std::uint32_t updateId_ = 0;
};

} // namespace biglybtc::media
