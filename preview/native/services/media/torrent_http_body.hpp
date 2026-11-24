// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "http_media_range.hpp"
#include "persistent_session.hpp"

#include <QByteArray>
#include <QFile>
#include <chrono>
#include <memory>
#include <variant>

namespace biglybtc::media {

class TorrentTranscode;

// One selected media response on the engine worker. The caller takes the next
// chunk when its transport is ready and cancels when the client disconnects.
class TorrentHttpBody final {
public:
  using OpenError = std::variant<storage::Error, MediaRangeError>;

  static std::expected<std::unique_ptr<TorrentHttpBody>, OpenError> open(
      std::weak_ptr<engine::PersistentSession>, engine::PersistentSession::VerifiedFileSelection,
      MediaHttpMethod, std::optional<std::string_view> range);
  ~TorrentHttpBody();
  TorrentHttpBody(const TorrentHttpBody&) = delete;
  TorrentHttpBody& operator=(const TorrentHttpBody&) = delete;

  const MediaHttpRange& response() const { return response_; }
  bool chunked() const { return progressive_; }
  std::string_view contentType() const { return contentType_; }
  const QByteArray& attachmentName() const { return attachmentName_; }
  const QByteArray& dlnaFeatures() const { return dlnaFeatures_; }
  // nullopt means pending. An empty vector marks the end of this response.
  std::expected<std::optional<std::vector<char>>, storage::Error> read(std::uint32_t maximum);
  void cancel();

private:
  friend class MediaContentRoutes;

  TorrentHttpBody(std::weak_ptr<engine::PersistentSession>, engine::PersistentSession::VerifiedFileSelection,
      MediaHttpRange);
  static std::expected<std::unique_ptr<TorrentHttpBody>, OpenError> openTranscoded(
      std::weak_ptr<engine::PersistentSession>, engine::PersistentSession::VerifiedFileSelection,
      std::shared_ptr<QFile>, std::uint64_t bytes, MediaHttpMethod, std::optional<std::string_view> range);
  static std::expected<std::unique_ptr<TorrentHttpBody>, OpenError> openProgressive(
      std::weak_ptr<engine::PersistentSession>, engine::PersistentSession::VerifiedFileSelection,
      std::shared_ptr<QFile>, std::weak_ptr<TorrentTranscode>, MediaHttpMethod, std::optional<std::string_view> range);
  std::expected<std::optional<std::vector<char>>, storage::Error> readTranscoded(std::uint32_t count);
  std::expected<std::optional<std::vector<char>>, storage::Error> readProgressive(std::uint32_t maximum);
  void releaseRead();

  std::weak_ptr<engine::PersistentSession> session_;
  std::optional<std::weak_ptr<const engine::PersistentSession::VerifiedFileSelection>> publication_;
  engine::PersistentSession::VerifiedFileSelection source_;
  MediaHttpRange response_;
  std::string_view contentType_ = "application/octet-stream";
  QByteArray attachmentName_, dlnaFeatures_;
  std::shared_ptr<QFile> transcoded_;
  std::weak_ptr<TorrentTranscode> transcodeJob_;
  std::uint64_t lastProgressiveSize_ = 0;
  bool progressive_ = false;
  std::uint64_t delivered_ = 0;
  std::optional<engine::PersistentSession::VerifiedFileReadTicket> pending_;
  std::uint32_t pendingBytes_ = 0;
  std::chrono::steady_clock::time_point nextRead_{};
  std::optional<storage::Error> terminalError_;
};

} // namespace biglybtc::media
