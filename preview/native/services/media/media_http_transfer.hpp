// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "torrent_http_body.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

namespace biglybtc::media {

// Sends a single selected response on an already-connected socket. The HTTP
// request and resource selection belong to the caller; the socket is borrowed.
class MediaHttpTransfer final : public QObject {
public:
  enum class State { Idle, Sending, Sent, Disconnected, TimedOut, Failed };

  explicit MediaHttpTransfer(QObject* parent = nullptr);
  ~MediaHttpTransfer() override;
  bool start(QTcpSocket&, std::unique_ptr<TorrentHttpBody>, std::chrono::milliseconds idleTimeout);
  State state() const { return state_; }

private:
  void pump();
  void terminate(State);

  QPointer<QTcpSocket> socket_;
  std::unique_ptr<TorrentHttpBody> body_;
  QTimer timer_;
  QElapsedTimer lastWrite_;
  std::chrono::milliseconds idleTimeout_{};
  State state_ = State::Idle;
  bool headerSent_ = false;
  bool bodyExhausted_ = false;
};

} // namespace biglybtc::media
