// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_device_discovery.hpp"
#include "media_http_date.hpp"

#include <QAbstractEventDispatcher>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QThread>
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>

namespace biglybtc::media {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr qsizetype maximumSearch = 1024;
constexpr std::size_t maximumQueue = 32;
constexpr std::uint16_t defaultSsdpPort = 1900;
const QHostAddress ssdpGroup(QStringLiteral("239.255.255.250"));
constexpr auto serverIdentity = "Server: BiglyBT/0.1 UPnP/1.0 BiglyBT/0.1\r\n";

struct Search {
  QByteArray target;
  unsigned maximumDelay;
};

std::optional<Search> parseSearch(const QByteArray& raw, QByteArrayView authority) {
  constexpr QByteArrayView start("M-SEARCH * HTTP/1.1\r\n");
  if (raw.size() > maximumSearch || !raw.startsWith(start) || raw.indexOf("\r\n\r\n") != raw.size() - 4) {
    return std::nullopt;
  }
  auto fields = QByteArrayView(raw).sliced(start.size());
  QByteArray host, method, target, mx;
  unsigned count = 0;
  while (fields != "\r\n") {
    const auto end = fields.indexOf("\r\n");
    if (++count > 16 || end <= 0) {
      return std::nullopt;
    }
    const auto line = fields.first(end);
    const auto colon = line.indexOf(':');
    if (colon <= 0) {
      return std::nullopt;
    }
    const auto value = line.sliced(colon + 1).trimmed();
    if (value.empty() || !std::ranges::all_of(value, [](unsigned char c) { return c >= 32 && c <= 126; })) {
      return std::nullopt;
    }
    const auto key = line.first(colon).toByteArray().toLower();
    QByteArray* destination = nullptr;
    if (key == "host") { destination = &host; }
    else if (key == "man") { destination = &method; }
    else if (key == "mx") { destination = &mx; }
    else if (key == "st") { destination = &target; }
    if (destination) {
      if (!destination->isEmpty()) {
        return std::nullopt;
      }
      *destination = value.toByteArray();
    }
    fields = fields.sliced(end + 2);
  }
  if (host != authority || method.compare("\"ssdp:discover\"", Qt::CaseInsensitive) != 0 ||
      target.isEmpty() || target.size() > 128 || mx.isEmpty()) {
    return std::nullopt;
  }
  unsigned delay = 0;
  const auto end = mx.constData() + mx.size();
  const auto number = std::from_chars(mx.constData(), end, delay);
  if (number.ec != std::errc{} || number.ptr != end || delay == 0 || delay > 5) {
    return std::nullopt;
  }
  return Search{std::move(target), delay * 1000};
}
} // namespace

MediaDeviceDiscovery::MediaDeviceDiscovery(QObject* parent)
    : QObject(parent), socket_(this), multicast_(this), timer_(this) {
  socket_.setProxy(QNetworkProxy::NoProxy);
  multicast_.setProxy(QNetworkProxy::NoProxy);
  connect(&socket_, &QUdpSocket::readyRead, this, [this] { step(); });
  connect(&multicast_, &QUdpSocket::readyRead, this, [this] { step(); });
  connect(&timer_, &QTimer::timeout, this, [this] { step(); });
}

MediaDeviceDiscovery::~MediaDeviceDiscovery() { stop(); }

bool MediaDeviceDiscovery::start(const MediaDeviceIdentity& identity, std::uint16_t httpPort, std::uint16_t udpPort) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() || !identity.valid() ||
      httpPort == 0 || socket_.state() == QAbstractSocket::BoundState ||
      !socket_.bind(QHostAddress::LocalHost, udpPort, QUdpSocket::DontShareAddress)) {
    return false;
  }
  begin(identity, QHostAddress::LocalHost, httpPort);
  return true;
}

bool MediaDeviceDiscovery::startLan(const MediaDeviceIdentity& identity, const MediaLanEndpoint& endpoint,
    std::uint16_t httpPort, std::uint16_t udpPort) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() || !identity.valid() ||
      httpPort == 0 || socket_.state() == QAbstractSocket::BoundState || !endpoint.available()) {
    return false;
  }
  if (udpPort == 0) { udpPort = defaultSsdpPort; }
  const auto interface = QNetworkInterface::interfaceFromIndex(endpoint.interfaceIndex());
  constexpr auto share = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
  if (!socket_.bind(endpoint.address(), udpPort, share) || !multicast_.bind(ssdpGroup, udpPort, share) ||
      !multicast_.joinMulticastGroup(ssdpGroup, interface)) {
    stop();
    return false;
  }
  multicast_.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 16 * 1024);
  socket_.setMulticastInterface(interface);
  socket_.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
  socket_.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
  lan_ = endpoint;
  begin(identity, endpoint.address(), httpPort);
  nextInterfaceCheck_ = Clock::now() + 1s;
  advertise(true);
  return true;
}

void MediaDeviceDiscovery::begin(const MediaDeviceIdentity& identity, const QHostAddress& address, std::uint16_t port) {
  socket_.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 16 * 1024);
  udn_ = identity.udn().toLatin1();
  location_ = "http://" + address.toString().toLatin1() + ':' + QByteArray::number(port) + "/RootDevice.xml";
  timer_.start(25ms);
}

void MediaDeviceDiscovery::stop() {
  if (lan_ && socket_.state() == QAbstractSocket::BoundState && lan_->available()) { advertise(false); }
  timer_.stop();
  multicast_.close();
  socket_.close();
  queued_.clear();
  lan_.reset();
  udn_.clear();
  location_.clear();
}

std::array<QByteArray, 5> MediaDeviceDiscovery::targets() const {
  return {udn_, "upnp:rootdevice", "urn:schemas-upnp-org:device:MediaServer:1",
      "urn:schemas-upnp-org:service:ContentDirectory:1", "urn:schemas-upnp-org:service:ConnectionManager:1"};
}

QByteArray MediaDeviceDiscovery::uniqueName(const QByteArray& target) const {
  return target == udn_ ? udn_ : udn_ + "::" + target;
}

void MediaDeviceDiscovery::advertise(bool alive) {
  const auto port = socket_.localPort();
  const auto header = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:" + QByteArray::number(port) +
      (alive ? "\r\nNTS: ssdp:alive\r\n" : "\r\nNTS: ssdp:byebye\r\n");
  for (const auto& target : targets()) {
    auto data = header + "NT: " + target + "\r\nUSN: " + uniqueName(target) + "\r\n";
    if (alive) {
      data += "CACHE-CONTROL: max-age=3600\r\nLOCATION: " + location_ + "\r\n" + serverIdentity;
    }
    data += "\r\n";
    QNetworkDatagram packet(data, ssdpGroup, port);
    packet.setInterfaceIndex(static_cast<uint>(lan_->interfaceIndex()));
    socket_.writeDatagram(packet);
  }
  nextAnnounce_ = Clock::now() + 60s;
}

void MediaDeviceDiscovery::readSearches(QUdpSocket& socket) {
  const auto supported = targets();
  for (unsigned step = 0; step < 16 && socket.hasPendingDatagrams(); ++step) {
    const auto packet = socket.receiveDatagram(maximumSearch + 1);
    const auto peer = packet.senderAddress();
    const bool allowed = lan_ ? lan_->accepts(peer) && packet.interfaceIndex() == static_cast<uint>(lan_->interfaceIndex())
                              : peer == QHostAddress::LocalHost;
    if (!allowed || packet.senderPort() <= 0) {
      continue;
    }
    const auto authority = lan_ ? "239.255.255.250:" + QByteArray::number(socket_.localPort())
                                : QByteArray("239.255.255.250:1900");
    const auto request = parseSearch(packet.data(), authority);
    if (!request) {
      continue;
    }
    for (const auto& target : supported) {
      if (queued_.size() >= maximumQueue) {
        break;
      }
      if (request->target == target || request->target == "ssdp:all") {
        const auto delay = QRandomGenerator::global()->bounded(request->maximumDelay);
        queued_.push_back({target, peer, static_cast<std::uint16_t>(packet.senderPort()), Clock::now() + std::chrono::milliseconds(delay)});
      }
    }
  }
}

void MediaDeviceDiscovery::step() {
  if (socket_.state() != QAbstractSocket::BoundState) {
    return;
  }
  const auto now = Clock::now();
  if (lan_ && now >= nextInterfaceCheck_) {
    if (!lan_->available()) { stop(); return; }
    nextInterfaceCheck_ = now + 1s;
  }
  if (lan_ && now >= nextAnnounce_) { advertise(true); }
  std::erase_if(queued_, [&](const auto& reply) {
    if (reply.due > now) {
      return false;
    }
    const QByteArray data = "HTTP/1.1 200 OK\r\nUSN: " + uniqueName(reply.target) + "\r\nST: " + reply.target +
        "\r\nEXT:\r\nLocation: " + location_ + "\r\n" + serverIdentity +
        "Cache-Control: max-age=3600\r\nDate: " + mediaHttpDate() + "\r\nContent-Length: 0\r\n\r\n";
    socket_.writeDatagram(data, reply.peer, reply.port);
    return true;
  });
  readSearches(socket_);
  if (lan_) { readSearches(multicast_); }
}

} // namespace biglybtc::media
