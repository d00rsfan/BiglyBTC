// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "channel.hpp"
#include <QChronoTimer>
#include <chrono>
namespace biglybtc::services::tor {
// Post-authentication controller setup for a peer whose freshly started child
// belongs to the caller. The class owns only its control socket, never a process.
// No application activation, existing-daemon adoption, cookie lookup or shutdown.
class OwnershipHandshake final : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Authenticating, TakingOwnership, ResettingOwner,
      GettingInfo, Ready, Closed, Failed };
  Q_ENUM(State)
  explicit OwnershipHandshake(QObject* parent=nullptr);
  ~OwnershipHandshake() override;
  // Contract: port is a newly owned numeric127.0.0.1 control peer, not a discovered
  // or pre-existing daemon. Cookie/nonce are supplied, exactly32 bytes each.
  bool beginOwnedPeer(quint16 port,const QByteArray& cookie,const QByteArray& clientNonce,
      int deadlineMilliseconds=30000);
  void stop();
  State state() const { return state_; }
  bool deadlineActive() const { return deadline_.isActive(); }
  // Original ASCII getInfo() projection: last nonterminal250 reply minus its first
  // four characters. Typically "version=..."; empty if peer sent only250 OK.
  QByteArray rawVersionInfo() const { return versionInfo_; }
signals:
  void stateChanged(State state);
  void ready();
  void failed(QString reason); // Fixed text only; never peer transcripts/secrets.
  void closed();
private:
  using Clock=std::chrono::steady_clock;
  bool terminal() const;
  bool transition(State);
  bool expired();
  void issue(State,const QByteArray&);
  void consume(QByteArray);
  void finish(State,const QString& reason={});
  ControlChannel channel_;
  QChronoTimer deadline_;
  Clock::time_point expires_{};
  State state_=State::Idle;
  QByteArray versionInfo_,pendingInfo_;
  unsigned infoLines_=0;
  qsizetype infoBytes_=0;
};
}
