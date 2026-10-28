// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include "safecookie.hpp"
#include <QHostAddress>
#include <QNetworkProxy>
#include <QPointer>
#include <QThread>
#include <openssl/crypto.h>
#include <algorithm>
namespace biglybtc::services::tor {
namespace auth = plugins::tor;
ControlChannel::ControlChannel(QObject* parent) : QObject(parent), socket_(this), deadline_(this) {
  socket_.setProxy(QNetworkProxy::NoProxy);
  socket_.setReadBufferSize(4096);
  deadline_.setSingleShot(true);
  connect(&deadline_, &QTimer::timeout, this, [this] {
    if (state_ == State::Connecting || state_ == State::Challenge || state_ == State::Authentication) {
      finish(State::Failed, QStringLiteral("Control handshake timed out"));
    }
  });
  connect(&socket_, &QTcpSocket::connected, this, [this] {
    if (state_ != State::Connecting) { return; }
    const auto challenge = auth::challengeCommand(nonce_);
    if (!challenge) { finish(State::Failed, QStringLiteral("Invalid control nonce")); return; }
    if (!transition(State::Challenge)) { return; }
    writeLine(QByteArray(challenge->data(), static_cast<qsizetype>(challenge->size())));
  });
  connect(&socket_, &QTcpSocket::readyRead, this, &ControlChannel::consume);
  connect(&socket_, &QTcpSocket::errorOccurred, this, [this] {
    if (!terminal()) { finish(State::Failed, QStringLiteral("Control connection failed")); }
  });
  connect(&socket_, &QTcpSocket::disconnected, this, [this] {
    if (!terminal()) { finish(State::Failed, QStringLiteral("Control connection ended")); }
  });
}
ControlChannel::~ControlChannel() {
  // QObject connections and QPointers survive until base destruction. Quietly
  // close the socket while the timer, secrets and channel state still exist.
  deadline_.stop();
  state_ = State::Closed;
  socket_.blockSignals(true);
  socket_.abort();
  clearSecrets();
}
void ControlChannel::clearSecrets() {
  OPENSSL_cleanse(cookie_.data(), cookie_.size());
  OPENSSL_cleanse(nonce_.data(), nonce_.size());
}
bool ControlChannel::terminal() const { return state_ == State::Closed || state_ == State::Failed; }
bool ControlChannel::transition(State state) {
  state_ = state;
  QPointer<ControlChannel> guard(this);
  emit stateChanged(state);
  return guard && state_ == state;
}
bool ControlChannel::begin(quint16 port, const QByteArray& cookie, const QByteArray& clientNonce,
    int deadlineMilliseconds) {
  if (QThread::currentThread() != thread() || state_ != State::Idle) { return false; }
  if (port == 0 || cookie.size() != 32 || clientNonce.size() != 32
      || deadlineMilliseconds < 1 || deadlineMilliseconds > 30000) {
    finish(State::Failed, QStringLiteral("Invalid control connection parameters")); return false;
  }
  std::copy_n(reinterpret_cast<const std::uint8_t*>(cookie.constData()), 32, cookie_.begin());
  std::copy_n(reinterpret_cast<const std::uint8_t*>(clientNonce.constData()), 32, nonce_.begin());
  if (!transition(State::Connecting)) { return true; }
  QPointer<ControlChannel> guard(this);
  const bool bound = socket_.bind(QHostAddress::LocalHost, 0);
  if (!guard || terminal()) { return false; }
  if (!bound) {
    finish(State::Failed, QStringLiteral("Control loopback bind failed")); return false;
  }
  deadline_.start(deadlineMilliseconds);
  socket_.connectToHost(QHostAddress::LocalHost, port);
  return true;
}
bool ControlChannel::writeLine(const QByteArray& line) {
  if (socket_.bytesToWrite() + line.size() + 2 > 65536) {
    finish(State::Failed, QStringLiteral("Control output limit exceeded")); return false;
  }
  QPointer<ControlChannel> guard(this);
  const auto count = socket_.write(line);
  if (!guard || terminal()) { return false; }
  const auto ending = count == line.size() ? socket_.write("\r\n", 2) : -1;
  if (!guard || terminal()) { return false; }
  if (count != line.size() || ending != 2) {
    finish(State::Failed, QStringLiteral("Control write failed")); return false;
  }
  return true;
}
bool ControlChannel::sendCommand(const QByteArray& command) {
  if (QThread::currentThread() != thread() || state_ != State::Ready
      || command.isEmpty() || command.size() > 1024) { return false; }
  for (const unsigned char byte : command) { if (byte < 32 || byte > 126) { return false; } }
  return writeLine(command);
}
void ControlChannel::consume() {
  // Bound callbacks per event-loop turn so cancellation and UI work can run
  // even if an authenticated control peer continuously sends short replies.
  for (unsigned processed = 0; processed < 32 && !terminal(); ++processed) {
    const auto prefix = socket_.peek(1027);
    const auto newline = prefix.indexOf('\n');
    if (newline < 0) {
      if (socket_.bytesAvailable() > 1025) { finish(State::Failed, QStringLiteral("Control input line too long")); }
      return;
    }
    if (newline > 1025) { finish(State::Failed, QStringLiteral("Control input line too long")); return; }
    auto line = socket_.read(newline + 1);
    line.chop(1);
    if (line.endsWith('\r')) { line.chop(1); }
    if (line.contains('\r') || line.contains('\0') || line.size() > 1024) {
      finish(State::Failed, QStringLiteral("Invalid control reply framing")); return;
    }
    if (state_ == State::Challenge) {
      auto proof = auth::controllerCommand(cookie_, nonce_, {line.constData(), static_cast<std::size_t>(line.size())});
      clearSecrets();
      if (!proof) { finish(State::Failed, QStringLiteral("Control server authentication failed")); return; }
      QByteArray command(proof->data(), static_cast<qsizetype>(proof->size()));
      OPENSSL_cleanse(proof->data(), proof->size());
      if (!transition(State::Authentication)) { command.fill(0); return; }
      const bool sent = writeLine(command);
      command.fill(0);
      if (!sent) { return; }
    } else if (state_ == State::Authentication) {
      if (!auth::authenticationAccepted({line.constData(), static_cast<std::size_t>(line.size())})) {
        finish(State::Failed, QStringLiteral("Control client authentication failed")); return;
      }
      deadline_.stop();
      if (!transition(State::Ready)) { return; }
      QPointer<ControlChannel> guard(this);
      emit authenticated();
      if (!guard || state_ != State::Ready) { return; }
    } else if (state_ == State::Ready) {
      QPointer<ControlChannel> guard(this);
      emit replyLine(std::move(line));
      if (!guard || state_ != State::Ready) { return; }
    } else { finish(State::Failed, QStringLiteral("Unexpected control reply")); return; }
  }
  if (!terminal() && socket_.bytesAvailable() > 0) {
    QMetaObject::invokeMethod(this, &ControlChannel::consume, Qt::QueuedConnection);
  }
}
void ControlChannel::finish(State state, const QString& reason) {
  if (terminal()) { return; }
  state_ = state; // Guard synchronous socket callbacks before aborting.
  deadline_.stop(); clearSecrets(); socket_.abort();
  QPointer<ControlChannel> guard(this);
  emit stateChanged(state);
  if (!guard) { return; }
  if (state == State::Failed) { emit failed(reason); }
  else { emit closed(); }
}
void ControlChannel::stop() {
  if (QThread::currentThread() != thread()) { return; }
  finish(State::Closed);
}
}
