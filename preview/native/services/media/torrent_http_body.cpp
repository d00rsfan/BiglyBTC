// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrent_http_body.hpp"
#include "torrent_transcode.hpp"

#include <algorithm>
#include "state_file_io.hpp"
#ifdef _WIN32
#include <io.h>
#include <mutex>
#endif

namespace biglybtc::media {
namespace {
using Code = storage::ErrorCode;
constexpr std::uint32_t chunkLimit = 64 * 1024;
constexpr auto retryDelay = std::chrono::milliseconds(200);

std::ptrdiff_t readMediaAt(QFile& file, char* bytes, std::size_t size, std::uint64_t offset) {
#ifdef _WIN32
  // Windows' synchronous handle updates the file pointer. Concurrent HTTP
  // ranges share the pinned artifact and serialize only this read operation.
  static std::mutex readPosition;
  const std::lock_guard lock(readPosition);
  const auto handle = reinterpret_cast<storage::detail::StateHandle>(::_get_osfhandle(file.handle()));
#else
  const auto handle = file.handle();
#endif
  return storage::detail::readStateAt(handle, bytes, size, offset);
}

storage::Error retired() {
  return {Code::Conflict, "The HTTP media source has retired"};
}
} // namespace

std::expected<std::unique_ptr<TorrentHttpBody>, TorrentHttpBody::OpenError> TorrentHttpBody::open(
    std::weak_ptr<engine::PersistentSession> session, engine::PersistentSession::VerifiedFileSelection source,
    MediaHttpMethod method, std::optional<std::string_view> range) {
  const auto current = session.lock();
  if (!current) {
    return std::unexpected(OpenError{retired()});
  }
  const auto size = current->verifiedFileSize(source);
  if (!size) {
    return std::unexpected(OpenError{size.error()});
  }
  const auto plan = planMediaHttpRange(method, *size, range);
  if (!plan) {
    return std::unexpected(OpenError{plan.error()});
  }
  return std::unique_ptr<TorrentHttpBody>(new TorrentHttpBody(std::move(session), std::move(source), *plan));
}

std::expected<std::unique_ptr<TorrentHttpBody>, TorrentHttpBody::OpenError> TorrentHttpBody::openTranscoded(
    std::weak_ptr<engine::PersistentSession> session, engine::PersistentSession::VerifiedFileSelection source,
    std::shared_ptr<QFile> artifact, std::uint64_t bytes, MediaHttpMethod method, std::optional<std::string_view> range) {
  const auto current = session.lock();
  if (!current || !current->verifiedFileSize(source) || !artifact ||
      artifact->size() < 0 || static_cast<std::uint64_t>(artifact->size()) != bytes) {
    return std::unexpected(OpenError{retired()});
  }
  const auto plan = planMediaHttpRange(method, bytes, range);
  if (!plan) { return std::unexpected(OpenError{plan.error()}); }
  auto body = std::unique_ptr<TorrentHttpBody>(new TorrentHttpBody(std::move(session), std::move(source), *plan));
  body->transcoded_ = std::move(artifact);
  return body;
}

std::expected<std::unique_ptr<TorrentHttpBody>, TorrentHttpBody::OpenError> TorrentHttpBody::openProgressive(
    std::weak_ptr<engine::PersistentSession> session, engine::PersistentSession::VerifiedFileSelection source,
    std::shared_ptr<QFile> artifact, std::weak_ptr<TorrentTranscode> job, MediaHttpMethod method,
    std::optional<std::string_view> range) {
  const auto current = session.lock();
  if (!current || !current->verifiedFileSize(source) || !artifact || artifact->size() <= 0 || job.expired()) {
    return std::unexpected(OpenError{retired()});
  }
  if (range) { return std::unexpected(OpenError{MediaRangeError::Malformed}); }
  const MediaHttpRange plan{MediaHttpRange::Status::Whole, 0, 0, 0, method == MediaHttpMethod::Get};
  auto body = std::unique_ptr<TorrentHttpBody>(new TorrentHttpBody(std::move(session), std::move(source), plan));
  body->transcoded_ = std::move(artifact);
  body->transcodeJob_ = std::move(job);
  body->progressive_ = true;
  return body;
}

TorrentHttpBody::TorrentHttpBody(std::weak_ptr<engine::PersistentSession> session,
    engine::PersistentSession::VerifiedFileSelection source, MediaHttpRange response)
    : session_(std::move(session)), source_(std::move(source)), response_(response) {}

TorrentHttpBody::~TorrentHttpBody() {
  releaseRead();
}

void TorrentHttpBody::releaseRead() {
  if (pending_) {
    if (const auto current = session_.lock()) {
      current->cancelVerifiedFileRead(*pending_, source_.hash, source_.admission);
    }
    pending_.reset();
  }
}

void TorrentHttpBody::cancel() {
  releaseRead();
  terminalError_ = storage::Error{Code::Cancelled, "The HTTP media client disconnected"};
}

std::expected<std::optional<std::vector<char>>, storage::Error> TorrentHttpBody::readTranscoded(std::uint32_t count) {
  std::vector<char> bytes(count);
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto size = readMediaAt(*transcoded_, bytes.data() + done, bytes.size() - done,
        response_.offset + delivered_ + done);
    if (size <= 0) {
      terminalError_ = storage::Error{Code::Io, "The transcoded HTTP media read was incomplete"};
      return std::unexpected(*terminalError_);
    }
    done += static_cast<std::size_t>(size);
  }
  delivered_ += done;
  return bytes;
}

std::expected<std::optional<std::vector<char>>, storage::Error> TorrentHttpBody::readProgressive(std::uint32_t maximum) {
  const auto job = transcodeJob_.lock();
  const auto state = job ? job->state() : FfmpegJobState::Failed;
  const auto length = transcoded_->size();
  if ((state != FfmpegJobState::Running && state != FfmpegJobState::Produced) || length < 0 ||
      static_cast<std::uint64_t>(length) < lastProgressiveSize_) {
    terminalError_ = retired();
    return std::unexpected(*terminalError_);
  }
  lastProgressiveSize_ = static_cast<std::uint64_t>(length);
  if (!response_.sendBody || (state == FfmpegJobState::Produced && delivered_ == lastProgressiveSize_)) {
    return std::vector<char>{};
  }
  if (delivered_ == lastProgressiveSize_) { return std::nullopt; }
  return readTranscoded(static_cast<std::uint32_t>(std::min<std::uint64_t>(maximum, lastProgressiveSize_ - delivered_)));
}

std::expected<std::optional<std::vector<char>>, storage::Error> TorrentHttpBody::read(std::uint32_t maximum) {
  if (terminalError_) {
    return std::unexpected(*terminalError_);
  }
  if (publication_ && publication_->expired()) {
    releaseRead();
    terminalError_ = storage::Error{Code::Conflict, "The HTTP media publication has been withdrawn"};
    return std::unexpected(*terminalError_);
  }
  const auto current = session_.lock();
  const auto size = current ? current->verifiedFileSize(source_) : std::unexpected(retired());
  if (progressive_ && size) {
    if (maximum == 0 || maximum > chunkLimit) {
      return std::unexpected(storage::Error{Code::InvalidFormat, "The HTTP media read exceeds its chunk capacity"});
    }
    return readProgressive(maximum);
  }
  const auto bodySize = transcoded_ ? static_cast<std::uint64_t>(transcoded_->size()) : size.value_or(0);
  if (!size || bodySize != response_.fileSize) {
    releaseRead();
    terminalError_ = size ? retired() : size.error();
    return std::unexpected(*terminalError_);
  }
  if (!response_.sendBody || delivered_ == response_.length) {
    return std::vector<char>{};
  }
  if (maximum == 0 || maximum > chunkLimit || (pending_ && maximum < pendingBytes_)) {
    return std::unexpected(storage::Error{Code::InvalidFormat, "The HTTP media read exceeds its chunk capacity"});
  }
  const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(maximum, response_.length - delivered_));
  if (transcoded_) { return readTranscoded(count); }
  if (!pending_) {
    if (std::chrono::steady_clock::now() < nextRead_) {
      return std::nullopt;
    }
    auto read = current->requestVerifiedFileRead(source_.hash, source_.admission, source_.file,
        response_.offset + delivered_, count);
    if (!read) {
      if (read.error().code == Code::Busy || read.error().code == Code::Missing) {
        nextRead_ = std::chrono::steady_clock::now() + retryDelay;
        return std::nullopt;
      }
      terminalError_ = read.error();
      return std::unexpected(*terminalError_);
    }
    pending_ = *read;
    pendingBytes_ = count;
  }
  auto result = current->takeVerifiedFileRead(*pending_, source_.hash, source_.admission);
  if (!result) {
    releaseRead();
    if (result.error().code == Code::Missing) {
      nextRead_ = std::chrono::steady_clock::now() + retryDelay;
      return std::nullopt;
    }
    terminalError_ = result.error();
    return std::unexpected(*terminalError_);
  }
  if (*result) {
    pending_.reset();
    const auto count = (*result)->size();
    if (count == 0 || count > pendingBytes_) {
      terminalError_ = storage::Error{Code::Conflict, "The HTTP media read did not match the planned range"};
      return std::unexpected(*terminalError_);
    }
    delivered_ += count;
  }
  return result;
}

} // namespace biglybtc::media
