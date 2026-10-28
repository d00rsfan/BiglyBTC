// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <array>
#include <cstdint>
namespace biglybtc::services::tor {
// One connection per instance. Every method and destruction belongs to its Qt
// owner thread. This owns a socket only, never a Tor process or proxy selection.
class ControlChannel final : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Connecting, Challenge, Authentication, Ready, Closed, Failed };
  Q_ENUM(State)
  explicit ControlChannel(QObject* parent = nullptr);
  ~ControlChannel() override;
  // Explicit loopback port and supplied cookie/nonce; host randomness and cookie
  // file access are separate. No DNS, proxy, password or direct fallback.
  bool begin(quint16 port, const QByteArray& cookie, const QByteArray& clientNonce,
      int deadlineMilliseconds = 30000);
  bool sendCommand(const QByteArray& command);
  void stop();
  State state() const { return state_; }
  bool deadlineActive() const { return deadline_.isActive(); }
signals:
  void stateChanged(State state);
  void authenticated();
  void replyLine(QByteArray line);
  void failed(QString reason); // Fixed diagnostic only, no transcript or secret.
  void closed();
private:
  bool transition(State state);
  bool terminal() const;
  bool writeLine(const QByteArray& line);
  void consume();
  void finish(State state, const QString& reason = {});
  void clearSecrets();
  QTcpSocket socket_;
  QTimer deadline_;
  State state_ = State::Idle;
  std::array<std::uint8_t, 32> cookie_{}, nonce_{};
};
}
