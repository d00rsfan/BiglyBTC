#include "native_activity_history.hpp"

#include <utility>

namespace biglybtc::ui {
namespace {
bool healthy(NativeSessionState state) {
  return state == NativeSessionState::Idle || state == NativeSessionState::Active
      || state == NativeSessionState::Retiring;
}
}
NativeActivitySnapshot::NativeActivitySnapshot(NativeSessionPresentation latest,
    std::vector<NativeActivityPoint> points) : latest_(std::move(latest)), points_(std::move(points)) {}

bool NativeActivityHistory::observe(NativeSessionPresentation incoming, quint64 expectedSession) {
  if (!incoming || !expectedSession || incoming->session != expectedSession) { return false; }
  if (latest_ && (expectedSession < latest_->session
      || incoming->applicationRun < latest_->applicationRun)) { return false; }
  const bool sameProfile = latest_ && latest_->session == expectedSession;
  const auto prior = sameProfile ? latest_ : NativeSessionPresentation{};
  if (!acceptNativeSession(prior, incoming, expectedSession)) { return false; }
  const bool sameEpoch = sameProfile && incoming->epoch == latest_->epoch;
  const bool adjacent = sameEpoch && incoming->sequence - latest_->sequence == 1;
  const bool known = incoming->sample && incoming->sample->rates;
  const bool actualNew = incoming->sample && (!sameEpoch || !latest_->sample
      || incoming->sample->elapsedNanoseconds > latest_->sample->elapsedNanoseconds);
  const bool append = known && actualNew && incoming->state != NativeSessionState::Uncertified;
  const bool connected = append && sameEpoch && adjacent && connectable_
      && healthy(incoming->state) && healthy(latest_->state);

  // No callbacks/allocations occur after admission and before ring commit.
  if (!sameEpoch) { begin_ = 0; size_ = 0; connectable_ = false; }
  if (append) {
    auto destination = (begin_ + size_) % MaximumPoints;
    if (size_ == MaximumPoints) { destination = begin_; begin_ = (begin_ + 1) % MaximumPoints; }
    else { ++size_; }
    points_[destination] = {incoming->sample->elapsedNanoseconds, *incoming->sample->rates, connected};
    points_[begin_].connectedToPrevious = false;
    connectable_ = healthy(incoming->state);
  } else {
    // A repeated sample cannot conceal a previously observed gap. Only the
    // next actual point can establish a new connectable predecessor.
    connectable_ = connectable_ && adjacent && known && healthy(incoming->state)
        && latest_ && healthy(latest_->state);
  }
  latest_ = std::move(incoming);
  return true;
}
NativeActivityPresentation NativeActivityHistory::snapshot() const {
  if (!latest_) { return {}; }
  std::vector<NativeActivityPoint> points;
  points.reserve(size_);
  for (std::size_t i = 0; i < size_; ++i) { points.push_back(points_[(begin_ + i) % MaximumPoints]); }
  return NativeActivityPresentation(new NativeActivitySnapshot(latest_, std::move(points)));
}
void NativeActivityHistory::clear() noexcept {
  latest_.reset(); begin_ = 0; size_ = 0; connectable_ = false;
}
} // namespace biglybtc::ui
