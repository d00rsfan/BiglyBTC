// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_renderer_session.hpp"

#include <QScopeGuard>
#include <algorithm>
#include <thread>

namespace biglybtc::app {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using State = media::MediaRendererTransport::State;

bool ending(State state) {
  return state == State::Stopping || state == State::Completing;
}
} // namespace

MediaRendererSession::~MediaRendererSession() {
  discovery_.cancel();
  if (playback_) {
    try {
      stop();
      const auto deadline = Clock::now() + 600ms;
      while (ending(playback_->transport->state()) && Clock::now() < deadline) {
        server_.poll();
        std::this_thread::sleep_for(5ms);
      }
      retire();
    } catch (...) {
      server_.clearRendererType();
      playback_.reset();
    }
  }
}

bool MediaRendererSession::find() {
  const auto& endpoint = server_.lanEndpoint();
  if (scanning_ || !endpoint || !discovery_.startLan(*endpoint, 2500ms)) { return false; }
  expires_.reset();
  scanning_ = true;
  return true;
}

std::optional<ui::MediaRenderers> MediaRendererSession::takeCatalogue() {
  if (scanning_ && !discovery_.active()) {
    scanning_ = false;
    ui::MediaRenderers devices;
    for (const auto& device : discovery_.devices()) { devices.insert(device.id, device.name); }
    if (!devices.isEmpty()) { expires_ = Clock::now() + 60s; }
    return devices;
  }
  if (expires_ && Clock::now() >= *expires_) {
    expires_.reset();
    discovery_.cancel();
    return ui::MediaRenderers{};
  }
  return std::nullopt;
}

bool MediaRendererSession::play(const QUuid& id, const media::MediaContentRoutes::Selection& file,
    const media::FfmpegJob* conversion) {
  const auto& endpoint = server_.lanEndpoint();
  if (playback_ || scanning_ || !expires_ || Clock::now() >= *expires_ || !endpoint) { return false; }
  const auto& devices = discovery_.devices();
  const auto found = std::ranges::find(devices, id, &media::MediaRendererDevice::id);
  if (found == devices.end()) { return false; }
  auto transport = std::make_unique<media::MediaRendererTransport>(found->avTransport, found->connectionManager, *endpoint);
  const QHostAddress rendererPeer(found->avTransport.host());
  if (conversion) {
    auto worker = std::make_shared<media::TorrentTranscode>(engine_, file);
    if (!worker->start(*conversion)) { return false; }
    playback_.emplace(Playback{file, std::move(transport), false, std::move(worker), {}, false,
        conversion->fragmentMp4, rendererPeer});
    state_ = ui::MediaPlaybackState::Preparing;
    return true;
  }
  const bool temporary = server_.publishedUrl(file).isEmpty();
  if (temporary && !server_.publish(file)) { return false; }
  auto rollback = qScopeGuard([&] { if (temporary) { server_.withdraw(file); } });
  auto resource = server_.rendererResource(file);
  if (!resource) { return false; }
  playback_.emplace(Playback{file, std::move(transport), temporary, {}, {}, false, false, rendererPeer});
  if (!startRenderer(std::move(*resource))) {
    playback_.reset();
    return false;
  }
  rollback.dismiss();
  state_ = ui::MediaPlaybackState::Starting;
  return true;
}

bool MediaRendererSession::startRenderer(media::MediaRendererResource resource) {
  playback_->url = resource.url;
  const auto url = QUrl::fromEncoded(resource.url);
  return playback_->transport->playNegotiated(url, std::move(resource.didl), std::move(resource.types),
      [this, current = resource.url] { return playback_ && currentResource() == current; }, 5s,
      [this, path = url.path(QUrl::FullyEncoded).toLatin1()](QStringView type) {
        return playback_ && server_.selectRendererType(playback_->rendererPeer, std::string_view(path), type);
      });
}

QByteArray MediaRendererSession::currentResource() const {
  return playback_->conversion ? server_.transcodedUrl(playback_->conversion) : server_.publishedUrl(playback_->file);
}

bool MediaRendererSession::stop() {
  if (!playback_) { return false; }
  if (playback_->conversion && playback_->url.isEmpty()) {
    playback_->cancelling = true;
    playback_->conversion->cancel();
    return true;
  }
  auto& transport = *playback_->transport;
  if (transport.state() == State::Playing) { return transport.stop(2s); }
  return ending(transport.state()) || transport.cancel();
}

void MediaRendererSession::retainPublication(const media::MediaContentRoutes::Selection& file) {
  if (playback_ && playback_->file.hash == file.hash && playback_->file.file == file.file &&
      playback_->file.engine == file.engine && playback_->file.admission == file.admission) {
    playback_->temporary = false;
  }
}

std::optional<ui::MediaPlaybackState> MediaRendererSession::takeState() {
  using View = ui::MediaPlaybackState;
  if (!playback_) { return std::nullopt; }
  if (playback_->conversion && playback_->url.isEmpty()) {
    using Conversion = media::FfmpegJobState;
    const auto current = playback_->conversion->state();
    const bool publishing = playback_->progressive && current == Conversion::Running;
    if (current == Conversion::Starting || current == Conversion::Stopping || (current == Conversion::Running && !publishing)) {
      return std::nullopt;
    }
    if (playback_->cancelling || current == Conversion::Cancelled) {
      retire();
      state_ = View::Stopped;
      return state_;
    }
    auto resource = current == Conversion::Produced || publishing
        ? server_.publishTranscode(playback_->file, playback_->conversion) : std::nullopt;
    if (publishing && !resource) { return std::nullopt; }
    if (!resource || !startRenderer(std::move(*resource))) {
      retire();
      state_ = View::Failed;
      return state_;
    }
  }
  auto& transport = *playback_->transport;
  if (transport.state() == State::Playing && currentResource().isEmpty()) { transport.stop(2s); }
  auto next = View::Starting;
  switch (transport.state()) {
    case State::Playing: next = View::Playing; break;
    case State::Stopping:
    case State::Completing: next = View::Stopping; break;
    case State::Failed: next = View::Failed; retire(); break;
    case State::Stopped:
    case State::Cancelled: next = View::Stopped; retire(); break;
    default: break;
  }
  if (next == state_) { return std::nullopt; }
  state_ = next;
  return next;
}

void MediaRendererSession::retire() {
  server_.clearRendererType();
  if (playback_->conversion) { server_.withdrawTranscode(playback_->conversion); }
  else if (playback_->temporary) { server_.withdraw(playback_->file); }
  playback_.reset();
}

} // namespace biglybtc::app
