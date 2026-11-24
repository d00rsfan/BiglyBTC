// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_content_routes.hpp"
#include "media_device_discovery.hpp"

#include <QEventLoop>
#include <QTcpServer>
#include <QTimer>
#include <set>

namespace biglybtc::media {

// Optional listener on the engine worker. Files are explicitly published.
// Loopback is the default, with an explicit selected local-interface mode.
class MediaContentServer final : public QObject {
public:
  struct Limits {
    std::chrono::milliseconds requestTimeout{3000};
    std::chrono::milliseconds writeTimeout{15000};
    std::size_t connections = 8;
  };

  explicit MediaContentServer(std::weak_ptr<engine::PersistentSession>,
      std::optional<MediaBasicCredentials> credentials = {});
  ~MediaContentServer() override;
  bool configureDevice(MediaDeviceIdentity);
  bool configureLan(MediaLanEndpoint);
  const std::optional<MediaLanEndpoint>& lanEndpoint() const { return lan_; }
  std::optional<MediaRendererResource> rendererResource(const MediaContentRoutes::Selection& file) const {
    return routes_.rendererResource(file, port());
  }
  bool allowEventCallback(const QUrl&);
  bool start(std::uint16_t port, Limits limits);
  void poll();
  void stop();
  std::uint16_t port() const { return server_.serverPort(); }
  bool startDiscovery(std::uint16_t port = 0);
  std::uint16_t discoveryPort() const { return discovery_.port(); }
  bool publish(const MediaContentRoutes::Selection& file) { return routes_.publish(file); }
  QByteArray publishedUrl(const MediaContentRoutes::Selection& file) const { return routes_.publishedUrl(file, port()); }
  bool withdraw(const MediaContentRoutes::Selection& file) { return routes_.withdraw(file); }
  bool selectRendererType(const QHostAddress& peer, std::string_view path, QStringView type) {
    return routes_.selectRendererType(peer, path, type);
  }
  void clearRendererType() { routes_.clearRendererType(); }
  std::optional<MediaRendererResource> publishTranscode(
      const MediaContentRoutes::Selection& file, std::shared_ptr<TorrentTranscode> job) {
    return routes_.publishTranscode(file, std::move(job), port());
  }
  bool withdrawTranscode(const std::shared_ptr<TorrentTranscode>& job) { return routes_.withdrawTranscode(job); }
  QByteArray transcodedUrl(const std::shared_ptr<TorrentTranscode>& job) const {
    return routes_.transcodedUrl(job, port());
  }
  std::optional<MediaBrowsePage> browse(const MediaBrowseRequest&);

private:
  class Connection;
  void accept();

  QEventLoop loop_;
  MediaContentRoutes routes_;
  QTcpServer server_;
  std::set<Connection*> connections_;
  Limits limits_;
  std::optional<MediaDeviceIdentity> identity_;
  MediaEventSubscriptions events_;
  QTimer eventTimer_;
  MediaDeviceDiscovery discovery_;
  std::optional<MediaLanEndpoint> lan_;
};

} // namespace biglybtc::media
