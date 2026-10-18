#include "engine_activity_history.hpp"

#include <utility>

namespace biglybtc::ui {

EngineActivitySnapshot::EngineActivitySnapshot(EngineStatisticsPresentation latest,
    std::vector<EngineActivityPoint> points)
    : latest_(std::move(latest)), points_(std::move(points)) {}

bool EngineActivityHistory::observe(EngineStatisticsPresentation incoming,
    quint64 expectedSession) {
  if (!acceptEngineStatistics(latest_, incoming, expectedSession)) { return false; }

  const bool sameOperation = latest_ && latest_->operation == incoming->operation;
  // Admission precedes allocation, and allocation precedes any ring mutation.
  // Never carry an old operation's sample into a new operation's terminal packet.
  if (sameOperation && incoming->state == EngineStatisticsState::Ended
      && !incoming->sample && latest_->sample) {
    auto normalized = std::make_shared<EngineStatisticsPacket>(*incoming);
    normalized->sample = latest_->sample;
    incoming = std::move(normalized);
  }

  bool append = incoming->sample && incoming->sample->rates
      && incoming->state != EngineStatisticsState::Preparing;
  if (sameOperation && incoming->state == EngineStatisticsState::Ended) {
    // An equal-time terminal sample was already admitted only if identical.
    // A missing terminal sample was normalized above and is also not new data.
    append = append && (!latest_->sample
        || incoming->sample->elapsedNanoseconds > latest_->sample->elapsedNanoseconds);
  }
  const bool connected = append && sameOperation
      && incoming->state == EngineStatisticsState::Observed
      && latest_->state == EngineStatisticsState::Observed
      && latest_->sample && latest_->sample->rates
      && incoming->sequence - latest_->sequence == 1;

  if (!sameOperation) { begin_ = 0; size_ = 0; }
  if (append) {
    std::size_t destination = (begin_ + size_) % MaximumPoints;
    if (size_ == MaximumPoints) {
      destination = begin_;
      begin_ = (begin_ + 1) % MaximumPoints;
    } else { ++size_; }
    points_[destination] = {incoming->sample->elapsedNanoseconds,
        *incoming->sample->rates, connected};
    // A clipped left edge has no retained predecessor, even when the actual
    // accepted observations were consecutive before eviction.
    points_[begin_].connectedToPrevious = false;
  }
  latest_ = std::move(incoming);
  return true;
}

EngineActivityPresentation EngineActivityHistory::snapshot() const {
  if (!latest_) { return {}; }
  std::vector<EngineActivityPoint> ordered;
  ordered.reserve(size_);
  for (std::size_t i = 0; i < size_; ++i) {
    ordered.push_back(points_[(begin_ + i) % MaximumPoints]);
  }
  return EngineActivityPresentation(new EngineActivitySnapshot(latest_, std::move(ordered)));
}

void EngineActivityHistory::clear() noexcept {
  latest_.reset();
  begin_ = 0;
  size_ = 0;
}

} // namespace biglybtc::ui
