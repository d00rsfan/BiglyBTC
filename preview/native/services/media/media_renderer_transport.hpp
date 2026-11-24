// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_lan_endpoint.hpp"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace biglybtc::media {

// A selected local AVTransport endpoint. The caller keeps the published media
// resource current and supplies its already serialized DIDL document.
class MediaRendererTransport final : public QObject {
public:
  enum class State { Idle, Negotiating, SettingUri, Starting, Playing, Stopping, Completing, Stopped, Cancelled, Failed };

  explicit MediaRendererTransport(QUrl control, QObject* parent = nullptr);
  MediaRendererTransport(QUrl control, QUrl connections, QObject* parent = nullptr);
  MediaRendererTransport(QUrl control, QUrl connections, MediaLanEndpoint);
  ~MediaRendererTransport() override;
  bool play(const QUrl& resource, QByteArray didl, std::uint32_t instance,
      std::function<bool()> current, std::chrono::milliseconds timeout);
  bool playNegotiated(const QUrl& resource, QByteArray didl, QStringList types,
      std::function<bool()> current, std::chrono::milliseconds timeout,
      std::function<bool(QStringView)> selectType = {});
  bool stop(std::chrono::milliseconds timeout);
  bool cancel();
  State state() const { return state_; }

private:
  enum class Action { ProtocolInfo, Prepare, CurrentConnections, SetUri, Play, Stop, Complete };

  static QLatin1StringView actionName(Action);
  static bool connectionAction(Action);
  bool endpointAllowed(const QUrl&, bool resource) const;
  bool begin(const QUrl&, QByteArray, std::function<bool()>, std::chrono::milliseconds);
  void setUri();
  void send(Action);
  void finished(QNetworkReply*, Action);
  bool collect();
  bool stillCurrent() const;
  void releaseReply();
  void complete(State);
  void end(State);

  QUrl endpoint_;
  QUrl connections_;
  std::optional<MediaLanEndpoint> lan_;
  QNetworkAccessManager network_;
  QTimer deadline_;
  QPointer<QNetworkReply> reply_;
  QByteArray input_;
  QUrl resource_;
  QByteArray didl_;
  QStringList offered_;
  QString chosen_;
  std::function<bool()> current_;
  std::function<bool(QStringView)> selectType_;
  std::uint32_t instance_ = 0;
  std::optional<std::uint32_t> connection_;
  bool prepared_ = false;
  State afterCompletion_ = State::Stopped;
  State state_ = State::Idle;
};

} // namespace biglybtc::media
