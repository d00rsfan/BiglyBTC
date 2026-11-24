// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrent_transcode.hpp"

#include <QAbstractEventDispatcher>
#include <QThread>
#include <limits>

namespace biglybtc::media {
namespace {
constexpr std::uint32_t chunkSize = 64 * 1024;
constexpr auto pumpInterval = std::chrono::milliseconds(20);
constexpr auto retryInterval = std::chrono::milliseconds(200);

QString retiredSource() {
  return QStringLiteral("The torrent source is no longer available");
}
} // namespace

TorrentTranscode::TorrentTranscode(std::weak_ptr<engine::PersistentSession> session, Input input)
    : session_(std::move(session)), input_(std::move(input)), process_(this), timer_(this) {
  connect(&timer_, &QTimer::timeout, this, [this] { pump(); });
}

TorrentTranscode::~TorrentTranscode() {
  releaseRead();
}

bool TorrentTranscode::sourceCurrent(const engine::PersistentSession& session) const {
  return session.verifiedFileSourceCurrent(input_.engine, input_.hash, input_.admission);
}

std::expected<void, QString> TorrentTranscode::start(const FfmpegJob& job) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() ||
      process_.state() != FfmpegJobState::Idle || ticket_) {
    return std::unexpected(QStringLiteral("The torrent transcode worker is unavailable"));
  }
  const auto session = session_.lock();
  if (!session || !sourceCurrent(*session)) {
    return std::unexpected(retiredSource());
  }
  auto first = session->requestVerifiedFileRead(input_.hash, input_.admission, input_.file, 0, chunkSize);
  if (!first) {
    return std::unexpected(QString::fromStdString(first.error().message));
  }
  ticket_ = *first;
  auto started = process_.start(job);
  if (!started) {
    releaseRead();
    return started;
  }
  output_ = job.output;
  timer_.start(pumpInterval);
  return {};
}

bool TorrentTranscode::cancel() {
  if (QThread::currentThread() != thread() || !process_.cancel()) {
    return false;
  }
  releaseRead();
  pending_.clear();
  timer_.stop();
  return true;
}

void TorrentTranscode::releaseRead() {
  if (!ticket_) {
    return;
  }
  if (const auto session = session_.lock()) {
    session->cancelVerifiedFileRead(*ticket_, input_.hash, input_.admission);
  }
  ticket_.reset();
}

void TorrentTranscode::fail(QString reason) {
  sourceError_ = std::move(reason);
  process_.cancel();
  releaseRead();
  pending_.clear();
  timer_.stop();
}

void TorrentTranscode::pump() {
  const auto processState = process_.state();
  if (processState != FfmpegJobState::Starting && processState != FfmpegJobState::Running) {
    releaseRead();
    pending_.clear();
    timer_.stop();
    return;
  }
  const auto session = session_.lock();
  if (!session || !sourceCurrent(*session)) {
    fail(retiredSource());
    return;
  }
  if (processState == FfmpegJobState::Starting || eof_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!ticket_ && pending_.empty()) {
    if (now < nextRead_) {
      return;
    }
    const auto next = session->requestVerifiedFileRead(input_.hash, input_.admission, input_.file, offset_, chunkSize);
    if (!next) {
      if (next.error().code == storage::ErrorCode::Busy || next.error().code == storage::ErrorCode::Missing) {
        nextRead_ = now + retryInterval;
      } else {
        fail(QString::fromStdString(next.error().message));
      }
      return;
    }
    ticket_ = *next;
  }

  if (ticket_) {
    auto completed = session->takeVerifiedFileRead(*ticket_, input_.hash, input_.admission);
    if (!completed) {
      releaseRead();
      if (completed.error().code == storage::ErrorCode::Missing) {
        nextRead_ = now + retryInterval;
      } else {
        fail(QString::fromStdString(completed.error().message));
      }
      return;
    }
    if (!*completed) {
      return;
    }
    ticket_.reset();
    pending_ = std::move(**completed);
    consumed_ = 0;
    if (pending_.empty()) {
      eof_ = process_.finishInput();
      if (!eof_) {
        fail(QStringLiteral("Cannot close the media process input"));
      }
      return;
    }
    if (pending_.size() > chunkSize || pending_.size() > std::numeric_limits<std::uint64_t>::max() - offset_) {
      fail(QStringLiteral("The verified file read exceeds the media input range"));
      return;
    }
  }

  const auto accepted = process_.push(QByteArrayView(pending_.data() + consumed_,
      static_cast<qsizetype>(pending_.size() - consumed_)));
  if (accepted < 0) {
    fail(QStringLiteral("The media process refused verified input"));
    return;
  }
  consumed_ += static_cast<std::size_t>(accepted);
  if (consumed_ == pending_.size()) {
    offset_ += pending_.size();
    pending_.clear();
  }
}

FfmpegJobState TorrentTranscode::state() const {
  const auto current = process_.state();
  if (!sourceError_.isEmpty() && current == FfmpegJobState::Cancelled) {
    return FfmpegJobState::Failed;
  }
  if (current == FfmpegJobState::Produced) {
    const auto session = session_.lock();
    if (!session || !sourceCurrent(*session)) {
      return FfmpegJobState::Failed;
    }
  }
  return current;
}

QString TorrentTranscode::diagnostics() const {
  if (!sourceError_.isEmpty()) {
    return sourceError_;
  }
  if (process_.state() == FfmpegJobState::Produced && state() == FfmpegJobState::Failed) {
    return retiredSource();
  }
  return process_.diagnostics();
}

QString TorrentTranscode::resultFile() const {
  if (QThread::currentThread() != thread() || state() != FfmpegJobState::Produced) {
    return {};
  }
  return process_.resultFile();
}

bool TorrentTranscode::matches(const Input& selection) const {
  return selection.engine == input_.engine && selection.hash == input_.hash &&
      selection.admission == input_.admission && selection.file == input_.file;
}

std::optional<TorrentTranscode::Result> TorrentTranscode::openResult(const Input& selection) const {
  if (!matches(selection)) { return std::nullopt; }
  if (QThread::currentThread() != thread() || state() != FfmpegJobState::Produced) { return std::nullopt; }
  auto file = process_.resultHandle();
  if (!file) { return std::nullopt; }
  return Result{std::move(file), output_};
}

std::shared_ptr<QFile> TorrentTranscode::openProgressive(const Input& selection) {
  if (QThread::currentThread() != thread() || !matches(selection)) { return {}; }
  const auto session = session_.lock();
  if (!session || !sourceCurrent(*session) || !sourceError_.isEmpty()) { return {}; }
  return process_.progressiveHandle();
}

} // namespace biglybtc::media
