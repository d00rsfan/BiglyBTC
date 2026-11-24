// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_device_description.hpp"
#include "media_lan_endpoint.hpp"

#include <QTimer>
#include <QUdpSocket>
#include <array>
#include <chrono>
#include <vector>

namespace biglybtc::media {

// SSDP responder on loopback or one explicitly selected local interface.
// Advertisements and queued replies share the life of the HTTP description.
class MediaDeviceDiscovery final : public QObject {
public:
  explicit MediaDeviceDiscovery(QObject* parent = nullptr);
  bool start(const MediaDeviceIdentity&, std::uint16_t httpPort, std::uint16_t udpPort);
  ~MediaDeviceDiscovery() override;
  bool startLan(const MediaDeviceIdentity&, const MediaLanEndpoint&, std::uint16_t httpPort, std::uint16_t udpPort);
  void stop();
  std::uint16_t port() const { return socket_.localPort(); }

private:
  struct Reply {
    QByteArray target;
    QHostAddress peer;
    std::uint16_t port;
    std::chrono::steady_clock::time_point due;
  };

  void begin(const MediaDeviceIdentity&, const QHostAddress&, std::uint16_t httpPort);
  std::array<QByteArray, 5> targets() const;
  QByteArray uniqueName(const QByteArray& target) const;
  void advertise(bool alive);
  void step();
  void readSearches(QUdpSocket&);

  QUdpSocket socket_;
  QUdpSocket multicast_;
  QTimer timer_;
  QByteArray udn_;
  QByteArray location_;
  std::optional<MediaLanEndpoint> lan_;
  std::chrono::steady_clock::time_point nextAnnounce_;
  std::chrono::steady_clock::time_point nextInterfaceCheck_;
  std::vector<Reply> queued_;
};

} // namespace biglybtc::media
