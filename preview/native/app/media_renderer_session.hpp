// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_content_server.hpp"
#include "media_renderer_discovery.hpp"
#include "media_renderer_presentation.hpp"
#include "media_renderer_transport.hpp"
#include "torrent_transcode.hpp"

namespace biglybtc::app {

class MediaRendererSession final {
public:
  MediaRendererSession(media::MediaContentServer& server, std::weak_ptr<engine::PersistentSession> engine)
      : server_(server), engine_(std::move(engine)) {}
  ~MediaRendererSession();
  bool find();
  std::optional<ui::MediaRenderers> takeCatalogue();
  bool play(const QUuid&, const media::MediaContentRoutes::Selection&, const media::FfmpegJob* conversion = nullptr);
  void retainPublication(const media::MediaContentRoutes::Selection&);
  bool stop();
  std::optional<ui::MediaPlaybackState> takeState();

private:
  struct Playback {
    media::MediaContentRoutes::Selection file;
    std::unique_ptr<media::MediaRendererTransport> transport;
    bool temporary = false;
    std::shared_ptr<media::TorrentTranscode> conversion;
    QByteArray url;
    bool cancelling = false;
    bool progressive = false;
    QHostAddress rendererPeer{};
  };

  bool startRenderer(media::MediaRendererResource);
  QByteArray currentResource() const;
  void retire();
  media::MediaContentServer& server_;
  std::weak_ptr<engine::PersistentSession> engine_;
  media::MediaRendererDiscovery discovery_;
  std::optional<std::chrono::steady_clock::time_point> expires_;
  bool scanning_ = false;
  std::optional<Playback> playback_;
  ui::MediaPlaybackState state_ = ui::MediaPlaybackState::Idle;
};

} // namespace biglybtc::app
