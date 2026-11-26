// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_renderer_discovery.hpp"

#include <QAbstractEventDispatcher>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QThread>
#include <QXmlStreamReader>
#include <algorithm>

namespace biglybtc::media {
namespace {
using namespace std::chrono_literals;
constexpr qsizetype maximumAnswer = 2048;
constexpr qsizetype maximumDescription = 48 * 1024;
constexpr std::size_t maximumProbes = 16;
constexpr auto rendererType = "urn:schemas-upnp-org:device:MediaRenderer:1";
constexpr QLatin1StringView xmlNamespace("urn:schemas-upnp-org:device-1-0");
constexpr QLatin1StringView transportType("urn:schemas-upnp-org:service:AVTransport:1");
constexpr QLatin1StringView connectionType("urn:schemas-upnp-org:service:ConnectionManager:1");

bool directUrl(const QUrl& url) {
  const QHostAddress address(url.host());
  return url.isValid() && url.scheme() == u"http" && address.protocol() == QAbstractSocket::IPv4Protocol &&
      address.toString() == url.host() &&
      url.port() > 0 && !url.authority().contains('@') && !url.hasQuery() && !url.hasFragment() &&
      url.path().startsWith(u'/') && url.toEncoded().size() <= 2048;
}

QUuid deviceId(QStringView name) {
  if (!name.startsWith(u"uuid:") || name.size() != 41) {
    return {};
  }
  return QUuid::fromString(name.sliced(5));
}

std::optional<std::pair<QUuid, QUrl>> parseAnswer(const QByteArray& packet, const QHostAddress& peer) {
  constexpr QByteArrayView status("HTTP/1.1 200 OK\r\n");
  if (packet.size() > maximumAnswer || !packet.startsWith(status) || packet.indexOf("\r\n\r\n") != packet.size() - 4) {
    return std::nullopt;
  }
  QByteArray target, location, usn;
  auto rest = QByteArrayView(packet).sliced(status.size());
  for (unsigned count = 0; rest != "\r\n"; ++count) {
    const auto end = rest.indexOf("\r\n");
    if (count >= 24 || end < 0) { return std::nullopt; }
    const auto line = rest.first(end);
    const auto colon = line.indexOf(':');
    if (colon <= 0 || !std::ranges::all_of(line, [](unsigned char c) { return c >= 32 && c <= 126; })) {
      return std::nullopt;
    }
    const auto key = line.first(colon).toByteArray().toLower();
    auto* value = key == "location" ? &location : key == "usn" ? &usn : key == "st" ? &target : nullptr;
    if (value) {
      const auto text = line.sliced(colon + 1).trimmed();
      if (text.empty() || !value->isEmpty()) { return std::nullopt; }
      *value = text.toByteArray();
    }
    rest = rest.sliced(end + 2);
  }
  if (target != rendererType || usn.size() < 41 || usn.sliced(41) != "::" + target) {
    return std::nullopt;
  }
  const auto id = deviceId(QString::fromLatin1(usn.first(41)));
  const auto url = QUrl::fromEncoded(location, QUrl::StrictMode);
  return !id.isNull() && directUrl(url) && url.host() == peer.toString() ? std::optional(std::pair{id, url}) : std::nullopt;
}

bool uniqueText(QXmlStreamReader& xml, QString& field, qsizetype limit) {
  if (!field.isEmpty()) { return false; }
  field = xml.readElementText().trimmed();
  return !field.isEmpty() && field.size() <= limit && !xml.hasError();
}

bool deviceElement(const QXmlStreamReader& xml, QStringView name) {
  return xml.name() == name && xml.namespaceUri() == xmlNamespace;
}

struct DeviceFields {
  QString type;
  QString id;
  QString friendly;
  QString transport;
  QString connection;
};

bool readService(QXmlStreamReader& xml, DeviceFields& fields) {
  QString type;
  QString control;
  while (xml.readNextStartElement()) {
    if (deviceElement(xml, u"serviceType")) {
      if (!uniqueText(xml, type, 128)) { return false; }
    } else if (deviceElement(xml, u"controlURL")) {
      if (!uniqueText(xml, control, 2048)) { return false; }
    } else {
      xml.skipCurrentElement();
    }
  }
  auto* endpoint = type.compare(transportType, Qt::CaseInsensitive) == 0 ? &fields.transport :
      type.compare(connectionType, Qt::CaseInsensitive) == 0 ? &fields.connection : nullptr;
  if (endpoint) {
    if (control.isEmpty() || !endpoint->isEmpty()) { return false; }
    *endpoint = std::move(control);
  }
  return !xml.hasError();
}

bool readDevice(QXmlStreamReader& xml, DeviceFields& fields) {
  bool services = false;
  while (xml.readNextStartElement()) {
    if (deviceElement(xml, u"deviceType")) {
      if (!uniqueText(xml, fields.type, 128)) { return false; }
    } else if (deviceElement(xml, u"UDN")) {
      if (!uniqueText(xml, fields.id, 41)) { return false; }
    } else if (deviceElement(xml, u"friendlyName")) {
      if (!uniqueText(xml, fields.friendly, 256)) { return false; }
    } else if (deviceElement(xml, u"serviceList")) {
      if (services) { return false; }
      services = true;
      while (xml.readNextStartElement()) {
        if (deviceElement(xml, u"service")) {
          if (!readService(xml, fields)) { return false; }
        } else {
          xml.skipCurrentElement();
        }
      }
    } else {
      xml.skipCurrentElement();
    }
  }
  return !xml.hasError() && services;
}

std::optional<MediaRendererDevice> parseDescription(const QByteArray& body, const QUrl& location, const QUuid& id) {
  QXmlStreamReader xml(body);
  xml.setEntityExpansionLimit(1);
  while (!xml.atEnd() && !xml.isStartElement()) {
    if (xml.readNext() == QXmlStreamReader::DTD) { return std::nullopt; }
  }
  if (!deviceElement(xml, u"root")) { return std::nullopt; }
  DeviceFields fields;
  QString urlBase;
  bool device = false;
  while (xml.readNextStartElement()) {
    if (deviceElement(xml, u"URLBase")) {
      if (!uniqueText(xml, urlBase, 2048)) { return std::nullopt; }
    } else if (deviceElement(xml, u"device")) {
      if (device || !readDevice(xml, fields)) { return std::nullopt; }
      device = true;
    } else {
      xml.skipCurrentElement();
    }
  }
  while (!xml.atEnd()) { xml.readNext(); }
  if (xml.hasError() || !device || fields.type != QLatin1StringView(rendererType) || deviceId(fields.id) != id ||
      fields.friendly.isEmpty() || fields.transport.isEmpty() || fields.connection.isEmpty()) {
    return std::nullopt;
  }
  const auto sameAuthority = [&](const QUrl& url) { return directUrl(url) && url.authority() == location.authority(); };
  const auto base = urlBase.isEmpty() ? location : QUrl(urlBase, QUrl::StrictMode);
  const QUrl transportPath(fields.transport, QUrl::StrictMode);
  const QUrl connectionPath(fields.connection, QUrl::StrictMode);
  if (!sameAuthority(base) || !transportPath.isValid() || !connectionPath.isValid()) {
    return std::nullopt;
  }
  const auto transport = base.resolved(transportPath);
  const auto connection = base.resolved(connectionPath);
  if (!sameAuthority(transport) || !sameAuthority(connection) || transport == connection) {
    return std::nullopt;
  }
  return MediaRendererDevice{id, std::move(fields.friendly), transport, connection};
}
} // namespace

MediaRendererDiscovery::MediaRendererDiscovery(QObject* parent)
    : QObject(parent), socket_(this), network_(this), deadline_(this), descriptionDeadline_(this) {
  socket_.setProxy(QNetworkProxy::NoProxy);
  network_.setProxy(QNetworkProxy::NoProxy);
  deadline_.setSingleShot(true);
  descriptionDeadline_.setSingleShot(true);
  connect(&socket_, &QUdpSocket::readyRead, this, [this] { readSearch(); });
  connect(&deadline_, &QTimer::timeout, this, [this] { releaseRequest(); });
  connect(&descriptionDeadline_, &QTimer::timeout, this, [this] { nextDescription(); });
}

MediaRendererDiscovery::~MediaRendererDiscovery() { releaseRequest(); }

bool MediaRendererDiscovery::start(std::uint16_t port, std::chrono::milliseconds timeout) {
  return begin(QHostAddress::LocalHost, QHostAddress::LocalHost, port, timeout, {});
}

bool MediaRendererDiscovery::startLan(const MediaLanEndpoint& endpoint, std::chrono::milliseconds timeout, std::uint16_t port) {
  if (!endpoint.available()) { return false; }
  return begin(endpoint.address(), QHostAddress(QStringLiteral("239.255.255.250")), port, timeout, endpoint);
}

bool MediaRendererDiscovery::begin(const QHostAddress& address, const QHostAddress& destination, std::uint16_t port,
    std::chrono::milliseconds timeout, std::optional<MediaLanEndpoint> endpoint) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() || active_ || port == 0 ||
      timeout < 100ms || timeout > 10s || !socket_.bind(address, 0, QUdpSocket::DontShareAddress)) {
    return false;
  }
  devices_.clear();
  lan_ = endpoint;
  selectedPort_ = port;
  active_ = true;
  const auto query = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:" + QByteArray::number(lan_ ? port : 1900) +
      "\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: " + rendererType + "\r\n\r\n";
  QNetworkDatagram packet(query, destination, port);
  if (lan_) {
    socket_.setMulticastInterface(QNetworkInterface::interfaceFromIndex(lan_->interfaceIndex()));
    socket_.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    socket_.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    packet.setInterfaceIndex(static_cast<uint>(lan_->interfaceIndex()));
  }
  if (socket_.writeDatagram(packet) != query.size()) {
    releaseRequest();
    return false;
  }
  deadline_.start(timeout);
  return true;
}

void MediaRendererDiscovery::cancel() {
  if (QThread::currentThread() == thread()) {
    releaseRequest();
    devices_.clear();
  }
}

void MediaRendererDiscovery::readSearch() {
  for (unsigned turn = 0; turn < 16 && active_ && socket_.hasPendingDatagrams(); ++turn) {
    const auto packet = socket_.receiveDatagram(maximumAnswer + 1);
    const auto peer = packet.senderAddress();
    const bool allowed = lan_ ? lan_->accepts(peer) && packet.senderPort() > 0 &&
        packet.interfaceIndex() == static_cast<uint>(lan_->interfaceIndex()) :
        peer == QHostAddress::LocalHost && packet.senderPort() == selectedPort_;
    if (!allowed || attempted_.size() >= maximumProbes) { continue; }
    const auto description = parseAnswer(packet.data(), peer);
    if (!description || !attempted_.insert(description->first).second) { continue; }
    pending_.push_back({description->first, description->second});
    if (!lan_) { socket_.close(); break; }
  }
  fetchDescription();
  if (active_ && socket_.hasPendingDatagrams()) {
    QTimer::singleShot(0, this, [this] { readSearch(); });
  }
}

void MediaRendererDiscovery::fetchDescription() {
  if (reply_ || pending_.empty()) { return; }
  if (lan_ && !lan_->available()) { releaseRequest(); return; }
  auto description = std::move(pending_.front());
  pending_.pop_front();
  expected_ = description.id;
  QNetworkRequest request(description.url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setRawHeader("Accept-Encoding", "identity");
  auto* reply = network_.get(request);
  reply_ = reply;
  reply->setReadBufferSize(maximumDescription + 1);
  connect(reply, &QNetworkReply::readyRead, this, [this] { readDescription(); });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    readDescription();
    if (reply_ != reply) { return; }
    if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
      if (auto device = parseDescription(body_, reply->url(), expected_)) { devices_.push_back(std::move(*device)); }
    }
    nextDescription();
  });
  if (lan_) { descriptionDeadline_.start(750ms); }
}

void MediaRendererDiscovery::readDescription() {
  if (reply_) {
    body_ += reply_->read(maximumDescription + 1 - body_.size());
    if (body_.size() > maximumDescription) { nextDescription(); }
  }
}

void MediaRendererDiscovery::releaseDescription() {
  descriptionDeadline_.stop();
  if (auto* reply = reply_.data()) {
    reply_.clear();
    reply->disconnect(this);
    if (!reply->isFinished()) { reply->abort(); }
    reply->deleteLater();
  }
  body_.clear();
  expected_ = QUuid{};
}

void MediaRendererDiscovery::nextDescription() {
  releaseDescription();
  if (lan_) { fetchDescription(); }
  else { releaseRequest(); }
}

void MediaRendererDiscovery::releaseRequest() {
  deadline_.stop();
  socket_.close();
  releaseDescription();
  lan_.reset();
  attempted_.clear();
  pending_.clear();
  active_ = false;
  selectedPort_ = 0;
}

} // namespace biglybtc::media
