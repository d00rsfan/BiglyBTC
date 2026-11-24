// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ffmpeg_process.hpp"
#include "persistent_session.hpp"

#include <QFile>
#include <chrono>
#include <memory>

namespace biglybtc::media {

// Runs on the session worker with a Qt event dispatcher. The job does not
// retain the engine or its profile lease.
class TorrentTranscode final : public QObject {
public:
  using Input = engine::PersistentSession::VerifiedFileSelection;

  TorrentTranscode(std::weak_ptr<engine::PersistentSession> session, Input input);
  ~TorrentTranscode() override;

  std::expected<void, QString> start(const FfmpegJob&);
  bool cancel();
  FfmpegJobState state() const;
  QString diagnostics() const;
  QString resultFile() const;

  struct Result {
    std::shared_ptr<QFile> artifact;
    FfmpegContainer format;
  };
  // Open the joined result for precisely the current source admission. A
  // retained descriptor keeps later HTTP readers bound to this exact result.
  std::optional<Result> openResult(const Input&) const;
  std::shared_ptr<QFile> openProgressive(const Input&);

private:
  void pump();
  void releaseRead();
  void fail(QString reason);
  bool sourceCurrent(const engine::PersistentSession&) const;
  bool matches(const Input&) const;

  std::weak_ptr<engine::PersistentSession> session_;
  Input input_;
  FfmpegProcess process_;
  FfmpegContainer output_ = FfmpegContainer::Matroska;
  QTimer timer_;
  std::optional<engine::PersistentSession::VerifiedFileReadTicket> ticket_;
  std::vector<char> pending_;
  std::size_t consumed_ = 0;
  std::uint64_t offset_ = 0;
  std::chrono::steady_clock::time_point nextRead_{};
  QString sourceError_;
  bool eof_ = false;
};

} // namespace biglybtc::media
