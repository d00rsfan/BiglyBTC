#pragma once
#include "owned_azureus_service.hpp"
#include "biglybtc/overall_statistics.hpp"

namespace biglybtc::app {
enum class OwnedDhtAccountingState {Disabled,Unavailable,Current,Final};
enum class OwnedDhtAccountingError {None,Missing,UnknownBytes,Range,Generation,Regression};
// One Host worker, one fixed explicit factory policy, one actual actor identity.
// No IO, clock, allocation, offset stitching or provider activation is owned.
class OwnedDhtAccounting final {
public:
  explicit OwnedDhtAccounting(bool configured)noexcept:configured_(configured),state_(configured?OwnedDhtAccountingState::Unavailable:OwnedDhtAccountingState::Disabled){}
  // A terminal snapshot is admitted only with service.joined+Closed and actor
  // Closed. Passing terminal=true alone cannot turn retained data into a seal.
  void observe(const std::optional<AzureusServiceObservation>&,bool terminal=false)noexcept;
  core::OverallDhtInput overallInput(bool ending=false)const noexcept;
  std::optional<std::array<std::int64_t,2>> historyCounters(bool ending=false)const noexcept;
  OwnedDhtAccountingState state()const noexcept{return state_;}
  OwnedDhtAccountingError error()const noexcept{return error_;}
  bool configured()const noexcept{return configured_;}
  std::uint64_t generation()const noexcept{return generation_;}
private:
  const bool configured_;
  OwnedDhtAccountingState state_;
  OwnedDhtAccountingError error_=OwnedDhtAccountingError::None;
  std::uint64_t generation_=0,sequence_=0;
  bool poisoned_=false;
  std::optional<core::OverallTransportCounters> lastKnown_;
  std::array<std::optional<core::OverallTransportCounters>,1> known_{},unknown_{};
};
} // namespace biglybtc::app
