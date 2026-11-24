// SPDX-License-Identifier: GPL-2.0-or-later
#include "rpc_http.hpp"

#include <QAbstractEventDispatcher>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <boost/asio/buffer.hpp>
#include <boost/beast/http.hpp>
#include <algorithm>
#include <sstream>

namespace biglybtc::plugins::rpc {
namespace {
namespace http = boost::beast::http;
using namespace std::chrono_literals;
constexpr std::size_t maxHeader = 8192;
constexpr std::size_t maxBody = 256 * 1024;
constexpr qsizetype maxReply = 1024 * 1024;
constexpr auto activeConnections = 8;
constexpr auto sessionHeader = "X-Transmission-Session-Id";
using Request = http::request<http::string_body>;

bool matchesDigest(QByteArrayView left, QByteArrayView right) {
  if (left.size() != right.size() || left.empty()) { return false; }
  unsigned char mismatch = 0;
  for (qsizetype i = 0; i < left.size(); ++i) { mismatch |= static_cast<unsigned char>(left[i] ^ right[i]); }
  return mismatch == 0;
}

bool basicUser(QByteArrayView user) {
  return !user.empty() && user.size() <= 128 && !user.contains(':') &&
      std::ranges::all_of(user, [](unsigned char c) { return c >= 33 && c <= 126; });
}

bool authorized(const Request& request, const HttpCredentials& credentials) {
  if (request.count(http::field::authorization) != 1) { return false; }
  const auto field = request[http::field::authorization];
  if (field.size() <= 6 || field.size() > 512 || !boost::beast::iequals(field.substr(0, 6), "Basic ")) {
    return false;
  }
  const auto bytes = QByteArray(field.data() + 6, static_cast<qsizetype>(field.size() - 6));
  const auto decoded = QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
  return decoded && credentials.matches(decoded.decoded);
}

bool exactLocalAuthority(const Request& request, quint16 port) {
  if (request.count(http::field::host) != 1) { return false; }
  const auto value = request[http::field::host];
  const auto asText = QByteArray(value.data(), static_cast<qsizetype>(value.size())).toLower();
  const auto suffix = ':' + QByteArray::number(port);
  return asText == "127.0.0.1" || asText == "localhost" || asText == "127.0.0.1" + suffix || asText == "localhost" + suffix;
}

Headers sessionHeaders(const Request& request) {
  Headers headers;
  if (request.count(sessionHeader)) {
    const auto bytes = request[sessionHeader];
    headers.insert(QStringLiteral("x-transmission-session-id"), bytes.empty() ? QStringLiteral("") : QString::fromLatin1(bytes));
  }
  if (request.count(http::field::cookie)) { headers.insert(QStringLiteral("cookie"), QString::fromLatin1(request[http::field::cookie])); }
  return headers;
}

struct Reply {
  http::status code;
  QByteArray data{};
  bool json = false;
  bool session = false;
};

Reply routeRpc(const Request& request, quint16 port, QByteArrayView session, const HttpCredentials& credentials,
    const Dispatcher& dispatcher) {
  using Result = http::status;
  if (!exactLocalAuthority(request, port) || request.count(sessionHeader) > 1 || request.count(http::field::cookie) > 1 ||
      request.count(http::field::content_length) > 1) {
    return {Result::bad_request};
  }
  if (!authorized(request, credentials)) { return {Result::unauthorized}; }
  const auto target = request.target();
  if (target != "/transmission/rpc" && target != "/rpc") { return {Result::not_found}; }
  if (request.method() != http::verb::post) { return {Result::method_not_allowed}; }
  if (request.chunked() || request.count(http::field::content_encoding) != 0 || request.count(http::field::expect) != 0) {
    return {Result::unsupported_media_type};
  }
  const auto path = QString::fromLatin1(target);
  const auto headers = sessionHeaders(request);
  const auto expected = QString::fromLatin1(session);
  const auto valid = sessionValid(path, headers, true, expected);
  if (!valid) { return {Result::bad_request}; }
  if (!*valid) {
    return {Result::conflict, "You_didn_t_set_the_X-Transmission-Session-Id", false, true};
  }
  if (request.body().empty()) {
    const auto validHeader = sessionValid(path, headers, false, expected);
    if (!validHeader || !*validHeader) { return {Result::conflict, {}, false, true}; }
  }
  const auto decoded = QJsonDocument::fromJson(QByteArrayView(request.body()).toByteArray());
  try {
    const auto result = process(decoded.isObject() ? std::optional{decoded.object()} : std::nullopt, dispatcher);
    if (!result) {
      const auto status = result.error() == OuterError::Permission ? Result::forbidden :
          result.error() == OuterError::MissingDispatcher ? Result::service_unavailable : Result::bad_request;
      return {status, {}, false, true};
    }
    auto json = QJsonDocument(*result).toJson(QJsonDocument::Compact);
    if (json.size() >= maxReply) { return {Result::internal_server_error}; }
    json += '\n';
    return {Result::ok, std::move(json), true, true};
  } catch (...) {
    return {Result::internal_server_error};
  }
}
} // namespace

std::optional<HttpCredentials> HttpCredentials::fromBasic(QByteArrayView user, QByteArrayView password) {
  if (!basicUser(user) || password.empty() || password.size() > 256) {
    return std::nullopt;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(user);
  hash.addData(QByteArrayView(":"));
  hash.addData(password);
  HttpCredentials result;
  result.digest_ = hash.result();
  return result;
}

std::optional<HttpCredentials> HttpCredentials::fromOriginal(QByteArrayView user, QByteArrayView sha1) {
  if (!basicUser(user) || sha1.size() != 20) { return std::nullopt; }
  HttpCredentials result;
  result.digest_ = sha1.toByteArray();
  result.originalUser_ = user.toByteArray();
  return result;
}

bool HttpCredentials::valid() const {
  return originalUser_.isEmpty() ? digest_.size() == 32 : digest_.size() == 20;
}

bool HttpCredentials::matches(QByteArrayView decoded) const {
  if (!valid()) { return false; }
  if (originalUser_.isEmpty()) {
    return matchesDigest(QCryptographicHash::hash(decoded, QCryptographicHash::Sha256), digest_);
  }
  const auto separator = decoded.indexOf(':');
  if (separator <= 0 || separator + 1 >= decoded.size()) { return false; }
  const auto password = QCryptographicHash::hash(decoded.sliced(separator + 1), QCryptographicHash::Sha1);
  const bool userMatches = matchesDigest(decoded.first(separator), originalUser_);
  const bool passwordMatches = matchesDigest(password, digest_);
  return userMatches && passwordMatches;
}

class LocalRpcHttpServer::Connection final : public QObject {
public:
  Connection(LocalRpcHttpServer& server, QTcpSocket* socket)
      : QObject(&server), server_(server), socket_(socket), deadline_(this) {
    socket_->setParent(this);
    socket_->setReadBufferSize(16384);
    parser_.header_limit(maxHeader);
    parser_.body_limit(maxBody);
    parser_.eager(true);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { socket_->abort(); deleteLater(); });
    connect(socket_, &QTcpSocket::disconnected, this, &QObject::deleteLater);
    connect(socket_, &QTcpSocket::readyRead, this, [this] { receive(); });
    deadline_.start(3s);
  }

  void receive() {
    if (responded_) { return; }
    buffer_ += socket_->readAll();
    if (received_ + static_cast<std::size_t>(buffer_.size()) > maxHeader + maxBody) {
      answer({http::status::payload_too_large});
      return;
    }
    boost::beast::error_code error;
    const auto read = parser_.put(boost::asio::buffer(buffer_.constData(), static_cast<std::size_t>(buffer_.size())), error);
    received_ += read;
    buffer_.remove(0, static_cast<qsizetype>(read));
    if (error && error != http::error::need_more) {
      answer({error == http::error::body_limit ? http::status::payload_too_large : http::status::bad_request});
      return;
    }
    if (parser_.is_done()) {
      if (!buffer_.isEmpty()) { answer({http::status::bad_request}); return; }
      QPointer<Connection> guard(this);
      const auto dispatch = server_.dispatch_;
      const auto reply = routeRpc(parser_.get(), server_.port(), server_.session_, server_.credentials_, dispatch);
      if (guard) { answer(reply); }
    }
  }

private:
  void answer(const Reply& outcome) {
    responded_ = true;
    http::response<http::string_body> response(outcome.code, 11);
    response.keep_alive(false);
    response.set(http::field::cache_control, "no-store");
    if (outcome.session) {
      response.set(sessionHeader, server_.session_.toStdString());
      response.set(http::field::set_cookie, std::string(sessionHeader) + '=' + server_.session_.toStdString() +
          "; path=/; HttpOnly; SameSite=Strict");
    }
    if (outcome.code == http::status::unauthorized) { response.set(http::field::www_authenticate, "Basic realm=\"BiglyBTC\""); }
    if (outcome.code == http::status::method_not_allowed) { response.set(http::field::allow, "POST"); }
    response.set(http::field::content_type, outcome.json ? "application/json; charset=UTF-8" : "text/plain; charset=UTF-8");
    response.body() = outcome.data.toStdString();
    response.prepare_payload();
    std::ostringstream wire;
    wire << response;
    const auto data = std::move(wire).str();
    if (socket_->write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
      socket_->abort();
    } else {
      socket_->disconnectFromHost();
    }
  }

  LocalRpcHttpServer& server_;
  QTcpSocket* socket_;
  QTimer deadline_;
  http::request_parser<http::string_body> parser_;
  QByteArray buffer_;
  std::size_t received_ = 0;
  bool responded_ = false;
};

LocalRpcHttpServer::LocalRpcHttpServer(QObject* parent) : QObject(parent), listener_(this) {
  listener_.setProxy(QNetworkProxy::NoProxy);
  listener_.setMaxPendingConnections(activeConnections);
  connect(&listener_, &QTcpServer::newConnection, this, [this] { accept(); });
}

LocalRpcHttpServer::~LocalRpcHttpServer() { stop(); }

bool LocalRpcHttpServer::start(quint16 port, HttpCredentials credentials, Dispatcher dispatch) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() || listener_.isListening() ||
      !credentials.valid() || !dispatch || !listener_.listen(QHostAddress::LocalHost, port)) {
    return false;
  }
  credentials_ = std::move(credentials);
  dispatch_ = std::move(dispatch);
  session_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
  return true;
}

void LocalRpcHttpServer::stop() {
  listener_.close();
  while (!open_.empty()) { delete *open_.begin(); }
  while (listener_.hasPendingConnections()) { delete listener_.nextPendingConnection(); }
  credentials_ = {};
  dispatch_ = {};
  session_.clear();
}

void LocalRpcHttpServer::accept() {
  while (auto* socket = listener_.nextPendingConnection()) {
    if (open_.size() >= activeConnections || socket->peerAddress() != QHostAddress::LocalHost) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    auto* owner = new Connection(*this, socket);
    open_.insert(owner);
    connect(owner, &QObject::destroyed, this, [this, owner] { open_.erase(owner); });
    if (socket->bytesAvailable() > 0) { owner->receive(); }
  }
}

} // namespace biglybtc::plugins::rpc
