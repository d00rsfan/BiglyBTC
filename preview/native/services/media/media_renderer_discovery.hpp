// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_lan_endpoint.hpp"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QUrl>
#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <vector>

namespace biglybtc::media {

struct MediaRendererDevice {
  QUuid id;
  QString name;
  QUrl avTransport;
  QUrl connectionManager;
};

// Explicit local or selected-interface renderer discovery. Every response
// projects a same-peer HTTP description before it can enter the device list.
class MediaRendererDiscovery final : public QObject {
public:
  explicit MediaRendererDiscovery(QObject* parent = nullptr);
  ~MediaRendererDiscovery() override;
  bool start(std::uint16_t ssdpPort, std::chrono::milliseconds timeout);
  bool startLan(const MediaLanEndpoint&, std::chrono::milliseconds timeout, std::uint16_t ssdpPort = 1900);
  bool active() const { return active_; }
  const std::vector<MediaRendererDevice>& devices() const { return devices_; }
  void cancel();

private:
  struct Description {
    QUuid id;
    QUrl url;
  };
  bool begin(const QHostAddress& address, const QHostAddress& destination, std::uint16_t port,
      std::chrono::milliseconds timeout, std::optional<MediaLanEndpoint>);
  void readSearch();
  void fetchDescription();
  void readDescription();
  void releaseDescription();
  void nextDescription();
  void releaseRequest();

  QUdpSocket socket_;
  QNetworkAccessManager network_;
  QTimer deadline_;
  QTimer descriptionDeadline_;
  QPointer<QNetworkReply> reply_;
  QByteArray body_;
  QUuid expected_;
  std::optional<MediaLanEndpoint> lan_;
  std::set<QUuid> attempted_;
  std::deque<Description> pending_;
  std::uint16_t selectedPort_ = 0;
  bool active_ = false;
  std::vector<MediaRendererDevice> devices_;
};

} // namespace biglybtc::media
