// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <memory>

namespace biglybtc::services::tor {
struct OwnedTorChildOptions {
  QString executable;       // Absolute, no-follow regular file, explicitly supplied.
  QString workingDirectory; // Fresh absent leaf inside an owned private directory.
  int startupMilliseconds = 30000;
  int shutdownGraceMilliseconds = 1000;
};

// Linux-only owned process + existing SAFECOOKIE/TAKEOWNERSHIP handshake.
// Fixed DisableNetwork1/SocksPort0 configuration; no profile/daemon discovery,
// proxy activation, arbitrary extra arguments, detached process or PID adoption.
// All methods/destruction belong to this object's Qt thread.
class OwnedTorChild final : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Starting, Discovering, Authenticating, Ready,
                     Stopping, Stopped, Failed };
  Q_ENUM(State)
  explicit OwnedTorChild(QObject* parent = nullptr);
  ~OwnedTorChild() override;
  bool start(const OwnedTorChildOptions&);
  void stop(); // Graceful owned-child termination, then bounded kill escalation.
  State state() const;
  bool joined() const; // True only after Qt reaped that actual child (or no launch).
  qint64 processId() const; // Diagnostic only; never accepted as external authority.
  quint16 controlPort() const;
  QByteArray rawVersionInfo() const;
  bool sourceCurrent() const; // Exact retained chain/leaf/child proof, no mutation.
signals:
  void stateChanged(State);
  void ready();
  void failed(QString reason); // Fixed text; no cookie/proof/output transcripts.
  void finished(); // Terminal and actual child joined, including failure cleanup.
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
