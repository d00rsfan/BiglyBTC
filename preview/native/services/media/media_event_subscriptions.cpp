// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_event_subscriptions.hpp"
#include "media_original_type.hpp"

#include <QNetworkProxy>
#include <QTcpSocket>
#include <QUuid>
#include <QXmlStreamWriter>
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace biglybtc::media {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t maximumSubscriptions = 16;
constexpr std::size_t maximumCallbacks = 32;
constexpr auto maximumLifetime = 30min;
constexpr auto noticeTimeout = 3s;
constexpr qsizetype maximumReply = 1024;

bool validCallback(const QUrl& url) {
  const QHostAddress address(url.host());
  return url.isValid() && url.scheme() == QStringLiteral("http") && address == QHostAddress::LocalHost &&
      url.port() >= 1 && url.port() <= 65535 && !url.authority().contains('@') &&
      !url.hasFragment() && !url.hasQuery() && url.path().startsWith('/') && url.toEncoded().size() <= 512;
}

QByteArray properties(MediaControlService service, std::uint32_t revision) {
  QByteArray body;
  QXmlStreamWriter xml(&body);
  xml.writeStartDocument();
  xml.writeStartElement("e:propertyset");
  xml.writeNamespace("urn:schemas-upnp-org:event-1-0", "e");
  const auto write = [&](const QString& name, const QString& value) {
    xml.writeStartElement("e:property");
    xml.writeTextElement(name, value);
    xml.writeEndElement();
  };
  if (service == MediaControlService::ContentDirectory) {
    write(QStringLiteral("SystemUpdateID"), QString::number(revision));
  } else {
    write(QStringLiteral("SourceProtocolInfo"), QString::fromLatin1(mediaSourceProtocols()));
    write(QStringLiteral("SinkProtocolInfo"), {});
    write(QStringLiteral("CurrentConnectionIDs"), {});
  }
  xml.writeEndDocument();
  return body;
}

class Notice final {
public:
  Notice(const QUrl& endpoint, const QByteArray& sid, std::uint32_t sequence, const QByteArray& xml)
      : deadline_(Clock::now() + noticeTimeout) {
    wire_ = "NOTIFY " + endpoint.path(QUrl::FullyEncoded).toLatin1() +
        " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(endpoint.port()) +
        "\r\nContent-Type: text/xml; charset=UTF-8\r\nNT: upnp:event\r\nNTS: upnp:propchange\r\nSID: " + sid +
        "\r\nSEQ: " + QByteArray::number(sequence) + "\r\nContent-Length: " + QByteArray::number(xml.size()) +
        "\r\nConnection: close\r\n\r\n" + xml;
    socket_.setProxy(QNetworkProxy::NoProxy);
    socket_.setReadBufferSize(maximumReply + 1);
    socket_.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(endpoint.port()));
  }

  std::optional<bool> poll() {
    if (Clock::now() >= deadline_) {
      return false;
    }
    if (!sent_ && socket_.state() == QAbstractSocket::ConnectedState) {
      sent_ = true;
      if (socket_.write(wire_) != wire_.size()) {
        return false;
      }
      wire_.clear();
    }
    if (socket_.bytesAvailable() > 0) {
      input_ += socket_.read(maximumReply + 1 - input_.size());
    }
    if (input_.size() > maximumReply) {
      return false;
    }
    const auto end = input_.indexOf("\r\n");
    if (end >= 0) {
      const auto status = QByteArrayView(input_).first(end);
      return status == "HTTP/1.1 200" || status == "HTTP/1.0 200" ||
          status.startsWith("HTTP/1.1 200 ") || status.startsWith("HTTP/1.0 200 ");
    }
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
      return false;
    }
    return std::nullopt;
  }

private:
  QTcpSocket socket_;
  QByteArray wire_;
  QByteArray input_;
  Clock::time_point deadline_;
  bool sent_ = false;
};
} // namespace

struct MediaEventSubscriptions::Impl {
  struct Subscription {
    MediaControlService service;
    QHostAddress peer;
    QUrl callback;
    Clock::time_point expires;
    std::uint32_t sequence = 0;
    std::optional<std::uint32_t> notified;
    std::unique_ptr<Notice> notice;
  };

  void expire() {
    const auto now = Clock::now();
    std::erase_if(subscriptions, [&](const auto& item) { return item.second.expires <= now; });
  }

  std::set<QByteArray> allowedCallbacks;
  std::map<QByteArray, Subscription> subscriptions;
};

MediaEventSubscriptions::MediaEventSubscriptions() : impl_(std::make_unique<Impl>()) {}
MediaEventSubscriptions::~MediaEventSubscriptions() = default;

bool MediaEventSubscriptions::allowCallback(const QUrl& url) {
  if (!validCallback(url)) {
    return false;
  }
  const auto endpoint = url.toEncoded();
  if (impl_->allowedCallbacks.size() >= maximumCallbacks && !impl_->allowedCallbacks.contains(endpoint)) {
    return false;
  }
  impl_->allowedCallbacks.insert(endpoint);
  return true;
}

std::expected<MediaEventReply, MediaEventError> MediaEventSubscriptions::handle(
    const QHostAddress& peer, const MediaEventRequest& request) {
  using Error = MediaEventError;
  if (peer != QHostAddress::LocalHost) {
    return std::unexpected(Error::Malformed);
  }
  impl_->expire();
  if (request.method == MediaEventMethod::Unsubscribe) {
    if (request.callback || request.sid.isEmpty()) {
      return std::unexpected(Error::Malformed);
    }
    const auto found = impl_->subscriptions.find(request.sid);
    if (found == impl_->subscriptions.end() || found->second.peer != peer || found->second.service != request.service) {
      return std::unexpected(Error::UnknownSubscription);
    }
    impl_->subscriptions.erase(found);
    return MediaEventReply{};
  }
  const auto timeout = std::clamp(request.timeout, 1s, std::chrono::duration_cast<std::chrono::seconds>(maximumLifetime));
  const auto expires = Clock::now() + timeout;
  if (!request.sid.isEmpty()) {
    if (request.callback) {
      return std::unexpected(Error::Malformed);
    }
    const auto found = impl_->subscriptions.find(request.sid);
    if (found == impl_->subscriptions.end() || found->second.peer != peer || found->second.service != request.service) {
      return std::unexpected(Error::UnknownSubscription);
    }
    found->second.expires = expires;
    return MediaEventReply{request.sid, timeout};
  }
  if (!request.callback || !validCallback(*request.callback) ||
      !impl_->allowedCallbacks.contains(request.callback->toEncoded())) {
    return std::unexpected(Error::UnknownSubscription);
  }
  if (impl_->subscriptions.size() == maximumSubscriptions) {
    return std::unexpected(Error::Capacity);
  }
  const auto sid = "uuid:" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
  if (!impl_->subscriptions.emplace(sid,
      Impl::Subscription{request.service, peer, *request.callback, expires, 0, {}, {}}).second) {
    return std::unexpected(Error::Capacity);
  }
  return MediaEventReply{sid, timeout};
}

bool MediaEventSubscriptions::needsDirectoryRevision() const {
  return std::ranges::any_of(impl_->subscriptions, [](const auto& item) {
    return item.second.service == MediaControlService::ContentDirectory;
  });
}

void MediaEventSubscriptions::poll(std::optional<std::uint32_t> directoryRevision) {
  impl_->expire();
  for (auto at = impl_->subscriptions.begin(); at != impl_->subscriptions.end();) {
    auto& [sid, state] = *at;
    if (state.notice) {
      const auto delivered = state.notice->poll();
      if (!delivered) {
        ++at;
        continue;
      }
      if (!*delivered) {
        at = impl_->subscriptions.erase(at);
        continue;
      }
      state.notice.reset();
    }
    const auto next = state.service == MediaControlService::ContentDirectory ? directoryRevision : std::optional<std::uint32_t>{0};
    if (next && state.notified != next) {
      state.notice = std::make_unique<Notice>(state.callback, sid, state.sequence, properties(state.service, *next));
      state.notified = next;
      state.sequence = state.sequence == std::numeric_limits<std::uint32_t>::max() ? 1 : state.sequence + 1;
    }
    ++at;
  }
}

void MediaEventSubscriptions::clear() {
  impl_->subscriptions.clear();
  impl_->allowedCallbacks.clear();
}

} // namespace biglybtc::media
