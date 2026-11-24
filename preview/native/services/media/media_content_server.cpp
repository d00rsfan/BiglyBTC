// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_content_server.hpp"
#include "media_directory_control.hpp"
#include "media_http_transfer.hpp"
#include "media_http_date.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QNetworkProxy>
#include <QThread>

namespace biglybtc::media {
namespace {
using namespace std::chrono_literals;
constexpr auto maxHeader = static_cast<qsizetype>(maximumMediaHttpHeader);
constexpr qsizetype maxControlReply = 1024 * 1024;

QByteArray rejection(MediaRequestError reason) {
  QByteArray response;
  switch (reason) {
  case MediaRequestError::BadRequest:
  case MediaRequestError::RangeRejected:
    response = "HTTP/1.1 400 Bad Request\r\n";
    break;
  case MediaRequestError::Unauthorized:
    response = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"BiglyBT Media Server\"\r\n";
    break;
  case MediaRequestError::Forbidden:
    response = "HTTP/1.1 403 Forbidden\r\n";
    break;
  case MediaRequestError::NotFound:
    response = "HTTP/1.1 404 Not Found\r\n";
    break;
  case MediaRequestError::Unsupported:
    response = "HTTP/1.1 501 Not Implemented\r\n";
    break;
  }
  return response + "Content-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
}
} // namespace

class MediaContentServer::Connection final : public QObject {
public:
  Connection(MediaContentServer& owner, QTcpSocket* socket)
      : QObject(&owner), owner_(owner), socket_(socket), timer_(this), transfer_(this) {
    socket_->setParent(this);
    socket_->setReadBufferSize(maxHeader + 1);
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { close(); });
    connect(socket_, &QTcpSocket::readyRead, this, [this] { receive(); });
    connect(socket_, &QTcpSocket::disconnected, this, &QObject::deleteLater);
    timer_.start(owner_.limits_.requestTimeout);
  }

  void receive() {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
      deleteLater();
      return;
    }
    if (issued_) {
      close();
      return;
    }
    if (control_) {
      receiveControl();
      return;
    }

    input_ += socket_->read(maxHeader + 1 - input_.size());
    const auto split = input_.indexOf("\r\n\r\n");
    const auto headerSize = split < 0 ? input_.size() : split + 4;
    if (headerSize > maxHeader) {
      reject(MediaRequestError::BadRequest);
      return;
    }
    if (split < 0) {
      return;
    }
    if (input_.startsWith("SUBSCRIBE ") || input_.startsWith("UNSUBSCRIBE ")) {
      serveEvent();
      return;
    }
    if (input_.startsWith("POST ")) {
      auto control = owner_.routes_.controlRequest(socket_->peerAddress(),
          std::string_view(input_.constData(), static_cast<std::size_t>(headerSize)));
      if (!control) {
        reject(control.error());
        return;
      }
      control_ = std::move(*control);
      input_.remove(0, headerSize);
      socket_->setReadBufferSize(maximumMediaControlBody + 1);
      receiveControl();
      return;
    }
    issued_ = true;
    timer_.stop();
    const std::string_view request(input_.constData(), static_cast<std::size_t>(input_.size()));
    const auto description = owner_.routes_.descriptionRequest(socket_->peerAddress(), request);
    if (!description) {
      reject(description.error());
      return;
    }
    if (*description) {
      serveDescription(**description);
      return;
    }
    auto body = owner_.routes_.resolve(socket_->peerAddress(), request);
    input_.clear();
    if (!body) {
      reject(body.error());
    } else if (!transfer_.start(*socket_, std::move(*body), owner_.limits_.writeTimeout)) {
      close();
    }
  }

  void close() {
    timer_.stop();
    socket_->abort();
    deleteLater();
  }

private:
  void serveEvent() {
    if (!owner_.identity_) {
      reject(MediaRequestError::NotFound);
      return;
    }
    const auto request = owner_.routes_.eventRequest(socket_->peerAddress(),
        std::string_view(input_.constData(), static_cast<std::size_t>(input_.size())));
    if (!request) {
      reject(request.error());
      return;
    }
    const auto result = owner_.events_.handle(socket_->peerAddress(), *request);
    if (!result) {
      if (result.error() == MediaEventError::Malformed) {
        reject(MediaRequestError::BadRequest);
      } else {
        QByteArray response = result.error() == MediaEventError::Capacity ? "HTTP/1.1 503 Service Unavailable\r\n" :
            "HTTP/1.1 412 Precondition Failed\r\n";
        reply(response + "Content-Length: 0\r\nConnection: close\r\n\r\n");
      }
      return;
    }
    QByteArray response = "HTTP/1.1 200 OK\r\n";
    response += "DATE: " + mediaHttpDate() + "\r\n";
    if (!result->sid.isEmpty()) {
      response += "SID: " + result->sid + "\r\nTIMEOUT: Second-" + QByteArray::number(result->timeout.count()) + "\r\n";
    }
    reply(response + "Content-Length: 0\r\nConnection: close\r\n\r\n");
  }

  void serveDescription(const MediaDescriptionRequest& request) {
    if (!owner_.identity_) {
      reject(MediaRequestError::NotFound);
      return;
    }
    const auto xml = describeMediaDevice(*owner_.identity_, request.document);
    if (xml.isEmpty()) {
      close();
      return;
    }
    replyXml(200, xml, request.sendBody);
  }

  void receiveControl() {
    const auto length = control_->contentLength;
    if (input_.size() <= length) {
      input_ += socket_->read(length + 1 - input_.size());
    }
    if (input_.size() > length) {
      reject(MediaRequestError::BadRequest);
      return;
    }
    if (input_.size() != length) {
      return;
    }
    const auto result = controlMediaDevice(owner_, control_->service, control_->action, input_, control_->xbox);
    replyXml(result.status, result.xml, true);
  }

  void replyXml(unsigned status, const QByteArray& xml, bool sendBody) {
    if (xml.size() > maxControlReply) {
      close();
      return;
    }
    QByteArray response = status == 200 ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 500 Internal Server Error\r\n";
    response += "Content-Type: text/xml; charset=UTF-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: ";
    response += QByteArray::number(xml.size()) + "\r\n\r\n";
    if (sendBody) {
      response += xml;
    }
    reply(response);
  }

  void reject(MediaRequestError error) {
    reply(rejection(error));
  }

  void reply(const QByteArray& response) {
    issued_ = true;
    input_.clear();
    if (socket_->write(response) == response.size()) {
      timer_.start(owner_.limits_.writeTimeout);
      socket_->disconnectFromHost();
    } else {
      close();
    }
  }

  MediaContentServer& owner_;
  QTcpSocket* socket_;
  QTimer timer_;
  MediaHttpTransfer transfer_;
  QByteArray input_;
  std::optional<MediaControlRequest> control_;
  bool issued_ = false;
};

MediaContentServer::MediaContentServer(std::weak_ptr<engine::PersistentSession> session,
    std::optional<MediaBasicCredentials> credentials)
    : loop_(this), routes_(std::move(session), std::move(credentials)),
      server_(this), eventTimer_(this), discovery_(this) {
  server_.setProxy(QNetworkProxy::NoProxy);
  connect(&server_, &QTcpServer::newConnection, this, [this] { accept(); });
  connect(&eventTimer_, &QTimer::timeout, this, [this] {
    std::optional<std::uint32_t> revision;
    if (events_.needsDirectoryRevision()) {
      if (const auto page = browse({}); page) {
        revision = page->updateId;
      }
    }
    events_.poll(revision);
  });
}

MediaContentServer::~MediaContentServer() {
  stop();
}

bool MediaContentServer::configureDevice(MediaDeviceIdentity identity) {
  if (QThread::currentThread() != thread() || server_.isListening() || !identity.valid()) {
    return false;
  }
  identity_ = std::move(identity);
  return true;
}

bool MediaContentServer::configureLan(MediaLanEndpoint endpoint) {
  if (QThread::currentThread() != thread() || server_.isListening() || !endpoint.available()) {
    return false;
  }
  lan_ = endpoint;
  return true;
}

bool MediaContentServer::allowEventCallback(const QUrl& url) {
  return QThread::currentThread() == thread() && events_.allowCallback(url);
}

bool MediaContentServer::start(std::uint16_t port, Limits limits) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() ||
      server_.isListening() || limits.connections == 0 || limits.connections > 32 ||
      limits.requestTimeout < 100ms || limits.requestTimeout > 30s ||
      limits.writeTimeout < 100ms || limits.writeTimeout > 5min) {
    return false;
  }
  limits_ = limits;
  server_.setMaxPendingConnections(static_cast<int>(limits_.connections));
  if ((lan_ && !lan_->available()) || !server_.listen(lan_ ? lan_->address() : QHostAddress(QHostAddress::LocalHost), port)) {
    return false;
  }
  if (lan_) { routes_.configureLan(*lan_, server_.serverPort()); }
  if (identity_) {
    eventTimer_.start(100ms);
  }
  return true;
}

void MediaContentServer::poll() {
  if (QThread::currentThread() == thread() && server_.isListening()) {
    loop_.processEvents(QEventLoop::AllEvents, QDeadlineTimer(5ms));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

bool MediaContentServer::startDiscovery(std::uint16_t port) {
  if (QThread::currentThread() != thread() || !identity_ || !server_.isListening()) {
    return false;
  }
  return lan_ ? discovery_.startLan(*identity_, *lan_, server_.serverPort(), port)
              : discovery_.start(*identity_, server_.serverPort(), port);
}

void MediaContentServer::stop() {
  routes_.clearTranscodes();
  discovery_.stop();
  eventTimer_.stop();
  events_.clear();
  server_.close();
  while (!connections_.empty()) {
    auto* connection = *connections_.begin();
    connection->close();
    delete connection;
  }
  while (server_.hasPendingConnections()) {
    delete server_.nextPendingConnection();
  }
}

std::optional<MediaBrowsePage> MediaContentServer::browse(const MediaBrowseRequest& request) {
  if (QThread::currentThread() != thread() || !server_.isListening()) {
    return std::nullopt;
  }
  return routes_.browse(server_.serverPort(), request);
}

void MediaContentServer::accept() {
  while (auto* socket = server_.nextPendingConnection()) {
    if (connections_.size() >= limits_.connections || !routes_.acceptsPeer(socket->peerAddress()) ||
        (lan_ && !lan_->available())) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    auto* connection = new Connection(*this, socket);
    connections_.insert(connection);
    connect(connection, &QObject::destroyed, this, [this, connection] { connections_.erase(connection); });
    if (socket->bytesAvailable() > 0) {
      connection->receive();
    }
  }
}

} // namespace biglybtc::media
