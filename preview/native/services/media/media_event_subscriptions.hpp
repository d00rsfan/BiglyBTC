// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_directory_control.hpp"

#include <QHostAddress>
#include <QUrl>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>

namespace biglybtc::media {

enum class MediaEventMethod { Subscribe, Unsubscribe };
enum class MediaEventError { Malformed, UnknownSubscription, Capacity };

struct MediaEventRequest {
  MediaControlService service;
  MediaEventMethod method;
  QByteArray sid;
  std::optional<QUrl> callback;
  std::chrono::seconds timeout;
};

struct MediaEventReply {
  QByteArray sid;
  std::chrono::seconds timeout;
};

// Worker-confined subscriptions. An HTTP callback must be authorized by the
// owner before an incoming request can enroll it. Outbound jobs use only that
// exact numeric loopback endpoint.
class MediaEventSubscriptions final {
public:
  MediaEventSubscriptions();
  ~MediaEventSubscriptions();

  bool allowCallback(const QUrl&);
  std::expected<MediaEventReply, MediaEventError> handle(const QHostAddress&, const MediaEventRequest&);
  bool needsDirectoryRevision() const;
  void poll(std::optional<std::uint32_t> directoryRevision);
  void clear();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace biglybtc::media
