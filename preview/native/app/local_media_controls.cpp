// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_media_controls.hpp"

#include "ffmpeg_process.hpp"
#include "library_window.hpp"
#include "media_device_description.hpp"
#include "session_bridge.hpp"

#include <QPointer>

namespace biglybtc::app {

void bindLocalMediaControls(ui::LibraryWindow& window, SessionBridge& bridge,
    std::optional<LocalMediaInterface> network, std::optional<LocalMediaFfmpeg> ffmpeg) {
  std::shared_ptr<media::FfmpegJob> mp4;
  if (network && ffmpeg) {
    mp4 = std::make_shared<media::FfmpegJob>();
    mp4->program = {std::move(ffmpeg->executable), std::move(ffmpeg->sha256)};
    mp4->output = media::FfmpegContainer::Mp4;
  }
  window.setLocalMediaNetwork(network.has_value(), bool(mp4));
  using Window = ui::LibraryWindow;
  QObject::connect(&bridge, &SessionBridge::mediaRenderersReady, &window, &Window::setMediaRenderers);
  QObject::connect(&bridge, &SessionBridge::mediaPlaybackReady, &window, &Window::setMediaPlayback);
  QObject::connect(&window, &Window::findMediaRenderersRequested, &bridge, &SessionBridge::findLocalMediaRenderers);
  QObject::connect(&window, &Window::stopMediaPlaybackRequested, &bridge, &SessionBridge::stopLocalMediaPlayback);
  QObject::connect(&window, &Window::playMediaOnRendererRequested, &bridge,
      [&bridge, mp4 = std::move(mp4)](ui::Target target, int file, QUuid renderer, ui::MediaRendererEncoding encoding) {
    using Encoding = ui::MediaRendererEncoding;
    std::shared_ptr<const media::FfmpegJob> job;
    if (encoding != Encoding::Original) {
      if (!mp4) { return; }
      auto selected = std::make_shared<media::FfmpegJob>(*mp4);
      // Bounds and pixel modulus from the shipped vuzexcode Generic H.264 profiles.
      if (encoding == Encoding::Mp4_480p || encoding == Encoding::Mp4_720p) {
        selected->videoBounds = encoding == Encoding::Mp4_480p ? media::FfmpegVideoBounds{704, 480, 8} :
            media::FfmpegVideoBounds{1280, 720, 8};
        selected->audioBounds = media::FfmpegAudioBounds{48000, 2};
        selected->requireAudio = true;
      }
      selected->fragmentMp4 = encoding == Encoding::Mp4Stream;
      job = std::move(selected);
    }
    bridge.playLocalMediaOnRenderer(std::move(target), static_cast<std::size_t>(file), renderer, std::move(job));
  });
  QObject::connect(&bridge, &SessionBridge::localMediaReady, &window, &Window::setLocalMedia);
  QObject::connect(&window, &Window::localMediaRequested, &bridge,
      [owner = QPointer(&bridge), view = QPointer(&window), network = std::move(network)](bool running) {
    if (!owner || !view) { return; }
    const bool accepted = running
        ? owner->startLocalMedia({{}, QStringLiteral("BiglyBTC")}, 0, network.has_value(), network)
        : owner->stopLocalMedia();
    if (!accepted && view) {
      view->setMessage(Window::tr("Wait for the current operation before changing the media server."));
    }
  });
  QObject::connect(&window, &Window::localMediaFileRequested, &bridge,
      [owner = QPointer(&bridge), view = QPointer(&window)](ui::Target target, int file) {
    if (owner && view && !owner->publishLocalMediaFile(std::move(target), static_cast<std::size_t>(file)) && view) {
      view->setMessage(Window::tr("Wait for the current operation before publishing a media file."));
    }
  });
}

} // namespace biglybtc::app
