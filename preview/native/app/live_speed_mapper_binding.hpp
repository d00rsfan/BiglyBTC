#pragma once
#include "owned_azureus_service.hpp"
#include "persistent_session.hpp"
#include "biglybtc/passive_speed_mapper.hpp"
#include "biglybtc/source_rate_average.hpp"

namespace biglybtc::app {
enum class LiveSpeedFailure {None,EngineUnavailable,Alignment,ReceiptLoss,Clock,Input,Memory};
struct LiveSpeedMapperObservation {
  std::uint64_t epoch=0,revision=0;
  std::shared_ptr<const core::PassiveSpeedSnapshot> snapshot;
  bool current=false;
  LiveSpeedFailure failure=LiveSpeedFailure::EngineUnavailable;
};
// The Host worker alone calls this. Explicit steady milliseconds share the
// transport receipt's std::chrono::steady_clock domain. No IO/timer/probes.
class LiveSpeedMapperBinding final {
public:
  explicit LiveSpeedMapperBinding(std::uint64_t profileSession);
  // Caller consumes the current actor mailbox BEFORE adding a newer engine
  // delivery to preserve real receipt/rate bucket alignment where available.
  void observeAzureus(const std::optional<AzureusServiceObservation>&,std::int64_t steadyMillis)noexcept;
  void observeEngine(const engine::ApplicationPeerObservation&,std::int64_t steadyMillis)noexcept;
  void close()noexcept;
  LiveSpeedMapperObservation observation()const noexcept{return status_;}
private:
  bool rateValues(std::int64_t receiptMillis,std::int64_t& upload,std::int64_t& download)noexcept;
  void gap(std::int64_t,LiveSpeedFailure)noexcept;
  void capture(bool,LiveSpeedFailure)noexcept;
  void advance()noexcept;
  const std::uint64_t profile_;
  engine::AccountingIdentity identity_{};
  std::unique_ptr<core::PassiveSpeedMapper> mapper_;
  LiveSpeedMapperObservation status_;
  std::optional<core::SourceRateAverage> dataReceived_,protocolReceived_,dataSent_,protocolSent_;
  std::optional<core::EngineStatistics> previous_;
  std::uint64_t engineSequence_=0,rttGeneration_=0,rttSequence_=0;
  std::optional<services::azureus_dht::Address> previousRttPeer_;
  std::int64_t rateClock_=0,lastCounterClock_=0;
  std::optional<std::int64_t> sourceTick_,lastMapperTime_;
  bool engineCurrent_=false,actorCurrent_=false,differences_=false,failed_=false;
};
} // namespace biglybtc::app
