// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include "session.hpp"
#include <QHostAddress>
#include <QObject>
#include <memory>
#include <optional>
#include <string>

namespace biglybtc::services::i2p::i2cp {
// Explicit external-router TCP registration, NOT a usable/tunnel I2P session.
// Every call/destruction belongs to this QObject's thread. Nothing auto-starts.
class InitialChannel final:public QObject {
  Q_OBJECT
public:
  enum class State {Idle,Connecting,WaitingDate,DateReceived,Closed,Failed,Creating,SessionCreated,Reconfiguring};
  Q_ENUM(State)
  enum class Failure {None,InvalidEndpoint,InvalidVersion,InvalidDeadline,UnsupportedAuthentication,
    ConnectFailed,WriteFailed,ReadFailed,Deadline,Framing,UnsupportedMessage,UnexpectedMessage,
    InvalidDate,RouterDisconnected,TransportClosed,Resources,SessionRejected,SessionDestroyed,InvalidSessionId,UnsupportedTransport};
  Q_ENUM(Failure)
  struct Options {
    QHostAddress address; // Explicit numeric loopback only, no host resolution.
    quint16 port=0;       // Explicit caller-selected port; no discovery/default.
    std::string clientVersion; // Explicit nonempty ASCII, not a claimed core version.
    bool authenticationRequired=false; // true refuses before connecting.
    int deadlineMilliseconds=30000; // Absolute connect+date budget,1..30000.
    bool tlsRequired=false; // true refuses before TCP; no plaintext fallback.
  };
  explicit InitialChannel(QObject* parent=nullptr);
  ~InitialChannel() override;
  // Active begin refuses without changing the current attempt. Terminal/idle
  // attempts can explicitly restart. No retries or reconnects occur internally.
  bool begin(const Options&);
  void close();
  // No private key is retained. These synchronous signing calls require a
  // current date/registration, explicit date and no pending write/request.
  // Completion means only queued I/O; actual CREATED/UPDATED is separate.
  [[nodiscard]] ConfigResult<void> createSession(const ClientIdentity&,const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  [[nodiscard]] ConfigResult<void> reconfigureSession(const ClientIdentity&,const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  [[nodiscard]] std::optional<std::uint16_t> sessionId()const noexcept;
  [[nodiscard]] SessionOptions sessionOptions()const; // Last actual ack only.
  [[nodiscard]] State state()const noexcept;
  [[nodiscard]] Failure failure()const noexcept;
  [[nodiscard]] bool socketActive()const noexcept;
  [[nodiscard]] bool deadlineActive()const noexcept;
  // Passive observations only; never clock, session, identity or payload rights.
  [[nodiscard]] std::optional<SetDate> routerDate()const;
  [[nodiscard]] std::optional<Disconnect> routerDisconnect()const;
signals:
  // Coalesced queued CURRENT-state notifications. Intermediate states may be
  // omitted. A callback may stop/restart/delete this object synchronously.
  void stateChanged(State);
  void dateReceived();
  void sessionCreated();
  void sessionUpdated();
  void failed(Failure);
  void closed();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::i2p::i2cp
