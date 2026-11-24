// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_http_transfer.hpp"

#include <QAbstractEventDispatcher>
#include <QThread>

namespace biglybtc::media {
namespace {
using namespace std::chrono_literals;
constexpr auto interval = 20ms;
constexpr std::uint32_t maxChunk = 64 * 1024;

QByteArray responseHead(const TorrentHttpBody& body) {
  const auto& range = body.response();
  using Status = MediaHttpRange::Status;
  QByteArray result;
  switch (range.status) {
  case Status::Whole:
    result = "HTTP/1.1 200 OK\r\n";
    break;
  case Status::Partial:
    result = "HTTP/1.1 206 Partial Content\r\n";
    break;
  case Status::Unsatisfiable:
    result = "HTTP/1.1 416 Range Not Satisfiable\r\n";
    break;
  }
  result += body.chunked() ? "Accept-Ranges: none\r\n" : "Accept-Ranges: bytes\r\n";
  result += "Connection: close\r\nCache-Control: private, no-store\r\nContent-Type: ";
  const auto contentType = body.contentType();
  result.append(contentType.data(), static_cast<qsizetype>(contentType.size()));
  if (range.status != Status::Unsatisfiable && !body.dlnaFeatures().isEmpty()) {
    result += "\r\ncontentFeatures.dlna.org: " + body.dlnaFeatures();
    result += "\r\ntransferMode.dlna.org: Streaming";
  }
  if (!body.attachmentName().isEmpty()) {
    result += "\r\nContent-Disposition: attachment; filename*=UTF-8''" + body.attachmentName();
  }
  if (body.chunked()) { result += "\r\nTransfer-Encoding: chunked"; }
  else { result += "\r\nContent-Length: " + QByteArray::number(static_cast<qulonglong>(range.length)); }
  result += "\r\n";
  if (const auto value = range.contentRange()) {
    result += "Content-Range: " + QByteArray::fromStdString(*value) + "\r\n";
  }
  return result + "\r\n";
}
} // namespace

MediaHttpTransfer::MediaHttpTransfer(QObject* parent) : QObject(parent), timer_(this) {
  connect(&timer_, &QTimer::timeout, this, [this] { pump(); });
}

MediaHttpTransfer::~MediaHttpTransfer() {
  terminate(State::Disconnected);
}

bool MediaHttpTransfer::start(QTcpSocket& socket, std::unique_ptr<TorrentHttpBody> body,
    std::chrono::milliseconds idleTimeout) {
  if (QThread::currentThread() != thread() || socket.thread() != thread() ||
      !QAbstractEventDispatcher::instance() || state_ != State::Idle || !body ||
      socket.state() != QAbstractSocket::ConnectedState || socket.bytesToWrite() != 0 ||
      idleTimeout < interval || idleTimeout > 5min) {
    return false;
  }
  body_ = std::move(body);
  socket_ = &socket;
  idleTimeout_ = idleTimeout;
  state_ = State::Sending;
  connect(&socket, &QTcpSocket::disconnected, this, [this] { terminate(State::Disconnected); });
  connect(&socket, &QObject::destroyed, this, [this] { terminate(State::Disconnected); });
  connect(&socket, &QTcpSocket::bytesWritten, this, [this](qint64 count) {
    if (state_ == State::Sending && count > 0) {
      lastWrite_.restart();
    }
  });
  lastWrite_.start();
  timer_.start(interval);
  return true;
}

void MediaHttpTransfer::terminate(State state) {
  if (state_ != State::Sending) {
    return;
  }
  state_ = state;
  timer_.stop();
  body_.reset();
  if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState) {
    if (state == State::Sent) {
      socket_->disconnectFromHost();
    } else {
      socket_->abort();
    }
  }
}

void MediaHttpTransfer::pump() {
  if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) {
    terminate(State::Disconnected);
    return;
  }
  if (lastWrite_.elapsed() >= idleTimeout_.count()) {
    terminate(State::TimedOut);
    return;
  }
  if (socket_->bytesToWrite() > 0) {
    return;
  }
  if (bodyExhausted_) {
    terminate(State::Sent);
    return;
  }
  auto result = body_->read(maxChunk);
  if (!result) {
    terminate(State::Failed);
    return;
  }
  if (!*result) {
    return;
  }
  auto& bytes = **result;
  bodyExhausted_ = bytes.empty();
  QByteArray wire;
  if (!headerSent_) {
    wire = responseHead(*body_);
    headerSent_ = true;
  }
  if (!bytes.empty()) {
    if (body_->chunked()) { wire += QByteArray::number(static_cast<qulonglong>(bytes.size()), 16) + "\r\n"; }
    wire.append(bytes.data(), static_cast<qsizetype>(bytes.size()));
    if (body_->chunked()) { wire += "\r\n"; }
  } else if (body_->chunked() && body_->response().sendBody) {
    wire += "0\r\n\r\n";
  }
  if (!wire.isEmpty() && socket_->write(wire) != wire.size()) {
    terminate(State::Failed);
  }
}

} // namespace biglybtc::media
