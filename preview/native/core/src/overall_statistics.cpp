#include "biglybtc/overall_statistics.hpp"

#include <bit>
#include <cstdint>
#include <initializer_list>

namespace biglybtc::core {
namespace {
constexpr std::size_t at(SavedStatistic field) noexcept { return static_cast<std::size_t>(field); }
std::int64_t add(std::int64_t left, std::int64_t right) noexcept {
  return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(left) + static_cast<std::uint64_t>(right));
}
std::int64_t subtract(std::int64_t left, std::int64_t right) noexcept {
  return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right));
}
std::int32_t average(std::int64_t bytes, std::int64_t seconds) noexcept {
  if (seconds <= 1) { return 0; }
  // Positive divisor excludes INT64_MIN/-1; division truncates toward zero.
  const auto low = static_cast<std::uint32_t>(static_cast<std::uint64_t>(bytes / seconds));
  return std::bit_cast<std::int32_t>(low);
}
void accumulate(std::int64_t& total, std::int64_t current, std::int64_t& previous) noexcept {
  total = add(total, subtract(current, previous));
  previous = current;
  if (total < 0) { total = 0; }
}
bool valid(OverallDhtResolution value) noexcept {
  return value == OverallDhtResolution::Retry || value == OverallDhtResolution::Empty
      || value == OverallDhtResolution::Ready;
}
} // namespace

OverallStatistics::OverallStatistics(OverallValues loaded, OverallPeerCounters initialPeer,
    std::int64_t sessionStartMillis, std::int64_t loadMillis) noexcept
    : values_(loaded), lastUptime_(loadMillis / 1000), sessionStart_(sessionStartMillis) {
  snapshot_ = {values_[at(SavedStatistic::ProtocolUploaded)], values_[at(SavedStatistic::DataUploaded)],
      values_[at(SavedStatistic::ProtocolDownloaded)], values_[at(SavedStatistic::DataDownloaded)],
      values_[at(SavedStatistic::DhtUploaded)], values_[at(SavedStatistic::DhtDownloaded)],
      initialPeer.protocolSent, initialPeer.dataSent, initialPeer.protocolReceived, initialPeer.dataReceived, 0, 0};
}

std::expected<OverallUpdate, OverallError> OverallStatistics::update(OverallPeerCounters peer,
    OverallDhtInput dht, std::int64_t nowMillis, bool force) noexcept {
  if (!valid(dht.resolution) || dht.transports.size() > MaximumDhtTransports
      || (cachedDhtCount_ && *cachedDhtCount_ > dht.transports.size())) {
    return std::unexpected(OverallError::InvalidInput);
  }
  const auto now = nowMillis / 1000;
  if (now < lastUptime_) {
    lastUptime_ = now;
    return OverallUpdate{OverallUpdateDisposition::ClockBackwards, std::nullopt};
  }

  const auto received = add(peer.dataReceived, peer.protocolReceived);
  const auto sent = add(peer.dataSent, peer.protocolSent);
  accumulate(values_[at(SavedStatistic::Downloaded)], received, lastDownloaded_);
  accumulate(values_[at(SavedStatistic::Uploaded)], sent, lastUploaded_);
  accumulate(values_[at(SavedStatistic::DataDownloaded)], peer.dataReceived, lastPeer_.dataReceived);
  accumulate(values_[at(SavedStatistic::ProtocolDownloaded)], peer.protocolReceived, lastPeer_.protocolReceived);
  accumulate(values_[at(SavedStatistic::DataUploaded)], peer.dataSent, lastPeer_.dataSent);
  accumulate(values_[at(SavedStatistic::ProtocolUploaded)], peer.protocolSent, lastPeer_.protocolSent);

  if (!cachedDhtCount_) {
    if (dht.resolution == OverallDhtResolution::Empty) { cachedDhtCount_ = 0; }
    else if (dht.resolution == OverallDhtResolution::Ready) { cachedDhtCount_ = dht.transports.size(); }
  }
  OverallTransportCounters current;
  for (std::size_t i = 0; i < cachedDhtCount_.value_or(0); ++i) {
    if (!dht.transports[i]) { return std::unexpected(OverallError::TransportUnavailable); }
    current.sent = add(current.sent, dht.transports[i]->sent);
    current.received = add(current.received, dht.transports[i]->received);
  }
  accumulate(values_[at(SavedStatistic::DhtUploaded)], current.sent, lastDht_.sent);
  accumulate(values_[at(SavedStatistic::DhtDownloaded)], current.received, lastDht_.received);

  const auto elapsed = subtract(now, lastUptime_);
  if (elapsed > 100 || elapsed < 0) {
    lastUptime_ = now;
    return OverallUpdate{OverallUpdateDisposition::ClockGap, std::nullopt};
  }
  auto& uptime = values_[at(SavedStatistic::Uptime)];
  if (uptime < 0) { uptime = 0; }
  uptime = add(uptime, elapsed); // Deliberately no post-add clamp in original.
  lastUptime_ = now;
  snapshot_ = {values_[at(SavedStatistic::ProtocolUploaded)], values_[at(SavedStatistic::DataUploaded)],
      values_[at(SavedStatistic::ProtocolDownloaded)], values_[at(SavedStatistic::DataDownloaded)],
      values_[at(SavedStatistic::DhtUploaded)], values_[at(SavedStatistic::DhtDownloaded)],
      peer.protocolSent, peer.dataSent, peer.protocolReceived, peer.dataReceived,
      current.sent, current.received};
  ++acceptedTicks_; // Unsigned modulo2^32 reproduces Java int increment.
  const bool due = force || std::bit_cast<std::int32_t>(acceptedTicks_) % 10 == 0;
  return OverallUpdate{OverallUpdateDisposition::Accepted,
      due ? std::optional<OverallValues>{values_} : std::nullopt};
}

void OverallStatistics::setMark(std::int64_t nowMillis) noexcept {
  values_[at(SavedStatistic::MarkTime)] = nowMillis;
  values_[at(SavedStatistic::MarkDownloaded)] = values_[at(SavedStatistic::Downloaded)];
  values_[at(SavedStatistic::MarkUploaded)] = values_[at(SavedStatistic::Uploaded)];
  values_[at(SavedStatistic::MarkUptime)] = values_[at(SavedStatistic::Uptime)];
}
void OverallStatistics::clearMark() noexcept {
  for (const auto field : {SavedStatistic::MarkTime, SavedStatistic::MarkDownloaded,
                           SavedStatistic::MarkUploaded, SavedStatistic::MarkUptime}) {
    values_[at(field)] = 0;
  }
}
std::int64_t OverallStatistics::since(SavedStatistic total, SavedStatistic baseline) noexcept {
  auto& mark = values_[at(baseline)];
  if (mark > values_[at(total)]) { mark = values_[at(total)]; }
  return subtract(values_[at(total)], mark);
}
std::expected<OverallScalar, OverallError> OverallStatistics::querySince(OverallSince query) noexcept {
  switch (query) {
    case OverallSince::Downloaded:
      return OverallScalar{OverallScalarKind::Long, since(SavedStatistic::Downloaded, SavedStatistic::MarkDownloaded)};
    case OverallSince::Uploaded:
      return OverallScalar{OverallScalarKind::Long, since(SavedStatistic::Uploaded, SavedStatistic::MarkUploaded)};
    case OverallSince::Uptime:
      return OverallScalar{OverallScalarKind::Long, since(SavedStatistic::Uptime, SavedStatistic::MarkUptime)};
    case OverallSince::AverageDownload: {
      const auto uptime = since(SavedStatistic::Uptime, SavedStatistic::MarkUptime);
      const auto bytes = since(SavedStatistic::Downloaded, SavedStatistic::MarkDownloaded);
      return OverallScalar{OverallScalarKind::Integer, average(bytes, uptime)};
    }
    case OverallSince::AverageUpload: {
      const auto uptime = since(SavedStatistic::Uptime, SavedStatistic::MarkUptime);
      const auto bytes = since(SavedStatistic::Uploaded, SavedStatistic::MarkUploaded);
      return OverallScalar{OverallScalarKind::Integer, average(bytes, uptime)};
    }
  }
  return std::unexpected(OverallError::InvalidInput);
}
std::int64_t OverallStatistics::sessionUptime(std::int64_t nowMillis) const noexcept {
  return subtract(nowMillis, sessionStart_) / 1000;
}
OverallObservation OverallStatistics::observe() const noexcept {
  return {values_[at(SavedStatistic::Downloaded)], values_[at(SavedStatistic::Uploaded)],
      values_[at(SavedStatistic::Uptime)], values_[at(SavedStatistic::MarkTime)],
      average(values_[at(SavedStatistic::Downloaded)], values_[at(SavedStatistic::Uptime)]),
      average(values_[at(SavedStatistic::Uploaded)], values_[at(SavedStatistic::Uptime)]), snapshot_};
}
std::optional<std::array<std::int64_t, 4>> OverallStatistics::coreStatistics(
    OverallPeerCounters peer, bool started) const noexcept {
  if (!started) { return std::nullopt; }
  return std::array<std::int64_t, 4>{
      add(values_[at(SavedStatistic::ProtocolUploaded)], subtract(peer.protocolSent, lastPeer_.protocolSent)),
      add(values_[at(SavedStatistic::DataUploaded)], subtract(peer.dataSent, lastPeer_.dataSent)),
      add(values_[at(SavedStatistic::ProtocolDownloaded)], subtract(peer.protocolReceived, lastPeer_.protocolReceived)),
      add(values_[at(SavedStatistic::DataDownloaded)], subtract(peer.dataReceived, lastPeer_.dataReceived))};
}

} // namespace biglybtc::core
