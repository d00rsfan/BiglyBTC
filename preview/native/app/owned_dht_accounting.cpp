#include "owned_dht_accounting.hpp"
#include <limits>

namespace biglybtc::app {
void OwnedDhtAccounting::observe(const std::optional<AzureusServiceObservation>& value,bool terminal)noexcept{
  if(!configured_||poisoned_||state_==OwnedDhtAccountingState::Final){return;}
  const auto unavailable=[&](OwnedDhtAccountingError error,bool poison=false){state_=OwnedDhtAccountingState::Unavailable;error_=error;poisoned_=poison;known_[0].reset();};
  if(!value||!value->snapshot||!value->sequence||value->error||value->transportFailure){unavailable(OwnedDhtAccountingError::Missing);return;}
  const auto& transport=value->snapshot->transport();
  const bool live=value->current&&value->state==AzureusServiceState::Running&&transport.state()==azureus::TransportStatisticsState::Bound;
  const bool final=terminal&&value->joined&&value->state==AzureusServiceState::Closed&&transport.state()==azureus::TransportStatisticsState::Closed;
  if(!live&&!final){unavailable(OwnedDhtAccountingError::Missing);return;}
  if(!transport.generation()||(generation_&&transport.generation()!=generation_)){unavailable(OwnedDhtAccountingError::Generation,true);return;}
  if(value->sequence<sequence_){unavailable(OwnedDhtAccountingError::Regression,true);return;}
  const auto& counters=transport.counters();
  if(!counters.sentPayloadBytes||!counters.receivedPayloadBytes){unavailable(OwnedDhtAccountingError::UnknownBytes);return;}
  constexpr auto maximum=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if(*counters.sentPayloadBytes>maximum||*counters.receivedPayloadBytes>maximum){unavailable(OwnedDhtAccountingError::Range,true);return;}
  const core::OverallTransportCounters sample{static_cast<std::int64_t>(*counters.sentPayloadBytes),static_cast<std::int64_t>(*counters.receivedPayloadBytes)};
  if(lastKnown_&&(sample.sent<lastKnown_->sent||sample.received<lastKnown_->received)){unavailable(OwnedDhtAccountingError::Regression,true);return;}
  generation_=transport.generation();sequence_=value->sequence;lastKnown_=sample;known_[0]=sample;
  state_=final?OwnedDhtAccountingState::Final:OwnedDhtAccountingState::Current;error_=OwnedDhtAccountingError::None;
}
core::OverallDhtInput OwnedDhtAccounting::overallInput(bool ending)const noexcept{
  if(!configured_){return {core::OverallDhtResolution::Empty,{}};}
  const bool known=state_==OwnedDhtAccountingState::Current||(ending&&state_==OwnedDhtAccountingState::Final);
  // The original reducer caches provider count: a configured-but-unavailable
  // source must retain its one null cell, not masquerade as discovered Empty.
  return {core::OverallDhtResolution::Ready,known?std::span(known_):std::span(unknown_)};
}
std::optional<std::array<std::int64_t,2>> OwnedDhtAccounting::historyCounters(bool ending)const noexcept{
  if(!configured_){return std::array<std::int64_t,2>{0,0};}
  if(state_!=OwnedDhtAccountingState::Current&&!(ending&&state_==OwnedDhtAccountingState::Final)){return {};}
  return std::array{known_[0]->sent,known_[0]->received};
}
} // namespace biglybtc::app
