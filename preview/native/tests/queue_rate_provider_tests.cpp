#include "biglybtc/queue_rate_providers.hpp"

#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>

using namespace biglybtc::core;
namespace {
unsigned assertions = 0, families = 0;
void need(bool value, const char* why) { ++assertions; if (!value) { throw std::runtime_error(why); } }
QueueRateSample sample(std::uint64_t sequence, std::int64_t millis, std::int64_t bytes,
    QueueRateScope scope = QueueRateScope::OriginalCombined, std::uint64_t owner = 1) {
  return {{owner, sequence, millis}, scope, bytes};
}
QueueEtaSample eta(std::uint64_t sequence, std::int64_t mono, std::int64_t wall,
    std::int64_t peer, std::int64_t disk, std::int64_t rate) {
  return {{1, sequence, mono}, wall, peer, disk, rate, {}, {}};
}
void globalSourceWarmupAndNominalConversion() {
  QueueGlobalReceiveAverage source;
  need(!source.observe(sample(1, 0, 0)), "First actual cumulative value is a baseline");
  need(!source.observe(sample(2, 500, 512)), "No fabricated1500ms tick");
  need(source.observe(sample(3, 1500, 3072)) == 2, "First populated source slot");
  need(source.observe(sample(4, 3000, 9216)) == 3, "Partial population divides actual two slots");
  need(source.observe(sample(5, 4500, 9216)) == 2, "Actual zero delta participates");
  need(source.sampleCount() == 3, "Only genuine selected observations count");
}
void globalBoundedHistoryAndExtremes() {
  QueueGlobalReceiveAverage source;
  need(!source.observe(sample(1, 0, 0)), "Baseline");
  for (std::uint64_t i = 1; i <= 25; ++i) {
    need(source.observe(sample(i + 1, static_cast<std::int64_t>(i) * 1500,
        static_cast<std::int64_t>(i) * 1536)) == 1, "Exact sliding source rate");
  }
  need(source.sampleCount() == 10, "Fixed ten-slot bound");
  QueueGlobalReceiveAverage huge;
  need(!huge.observe(sample(1, 0, 0)), "Extreme baseline");
  need(huge.observe(sample(2, 1500, std::numeric_limits<std::int64_t>::max()))
      == std::numeric_limits<std::int32_t>::max(), "Java floating-to-int saturation");
}
void globalUnknownScopeResetAndStale() {
  QueueGlobalReceiveAverage source;
  need(!source.observe(sample(1, 0, 50000)), "No existing-byte burst");
  need(source.observe(sample(2, 1500, 51536)) == 1, "Actual delta");
  need(!source.observe(sample(1, 0, 0)) && source.sampleCount() == 1, "Stale input cannot mutate latest history");
  need(!source.observe(sample(3, 3000, 1, QueueRateScope::TargetPayload)), "Payload cannot supply combined gate");
}
void globalDiscontinuities() {
  QueueGlobalReceiveAverage source;
  need(!source.observe(sample(1, 0, 0)), "Baseline");
  need(source.observe(sample(2, 1500, 1536)) == 1, "One rate");
  auto missing = sample(3, 3000, 0); missing.receivedBytes.reset();
  need(!source.observe(missing) && source.sampleCount() == 0, "Missing is a gap");
  need(!source.observe(sample(4, 4500, 1536000)), "Return baseline cannot charge the gap");
  need(!source.observe(sample(5, 9000, 1536001)), "Long clock gap is unknown");
  need(!source.observe(sample(6, 10000, 1536001)), "Rebaseline after gap");
  need(!source.observe(sample(7, 11500, 3)), "Counter reset is unknown");
  need(!source.observe(sample(8, 12000, 10)), "Rebaseline after counter reset");
  need(!source.observe(sample(9, 11999, 11)), "Clock regression is unknown");
  need(!source.observe(sample(10, 13000, 100, QueueRateScope::OwnerCombined, 2)), "New actual owner starts fresh");
}
void payloadUsesSourceWindowAndNotInstantRate() {
  QueuePayloadRateSmoothing source;
  need(!source.observe(sample(1, 0, 900000, QueueRateScope::TargetPayload), 0), "Admission baseline");
  need(source.observe(sample(2, 1000, 903000, QueueRateScope::TargetPayload), 1) == 0, "Current bucket excluded");
  need(source.observe(sample(3, 2000, 906000, QueueRateScope::TargetPayload), 2) == 100, "Whole30s source denominator");
  need(source.observe(sample(4, 3000, 909000, QueueRateScope::TargetPayload), 3) == 200, "Actual source smoothing");
  need(!source.observe(sample(5, 4000, 909000, QueueRateScope::OwnerCombined), 4), "Combined cannot replace payload");
}
void smoothingOldIntervalAndPendingDelta() {
  QueuePayloadRateSmoothing source;
  need(!source.observe(sample(1, 0, 0, QueueRateScope::OriginalPayload), 0, 120), "120s baseline");
  need(source.policy() == SourceSmoothingPolicy{120, 2}, "Original clamp/interval");
  need(source.observe(sample(2, 1000, 1200, QueueRateScope::OriginalPayload), 1, 30) == 0,
       "Old interval gates new config");
  need(source.policy().windowSeconds == 120, "Not applied early");
  need(source.observe(sample(3, 2000, 2400, QueueRateScope::OriginalPayload), 2, 30) == 0,
       "New source ring excludes current interval");
  need(source.policy() == SourceSmoothingPolicy{30, 1}, "New setting applied at old gate");
  need(source.observe(sample(4, 3000, 3600, QueueRateScope::OriginalPayload), 3, 30) == 80,
       "Reconfiguration retained actual pending baseline");
  need(source.observe(sample(5, 3500, 4200, QueueRateScope::OriginalPayload), 3, 30) == 80,
       "Same source tick cannot charge twice");
}
void smoothingGapsAndBounds() {
  QueuePayloadRateSmoothing source;
  need(!source.observe(sample(1, 0, 0, QueueRateScope::TargetPayload), 0, -1), "Clamped baseline");
  need(source.policy() == SourceSmoothingPolicy{10, 1}, "Original minimum");
  need(source.observe(sample(2, 1000, 1000, QueueRateScope::TargetPayload), 1, 9999) == 0, "Max setting");
  need(source.policy() == SourceSmoothingPolicy{1800, 20}, "Original max and interval cap");
  need(!source.observe(sample(3, 5001, 2000, QueueRateScope::TargetPayload), 5), "Gap cannot fill source slots");
  need(!source.observe(sample(4, 6000, 1000000, QueueRateScope::TargetPayload), 6), "No gap-byte burst");
  need(!source.observe(sample(5, 7000, 999999, QueueRateScope::TargetPayload), 7), "Counter correction unavailable");
}
void sendChannelsKeepActualGlobalCombinedAndTargetPayloadOwnership() {
  QueueSendRateSmoothing global,target;
  need(!global.observe(sample(1,0,10000,QueueRateScope::OwnerCombined),0),"Actual combined send baseline");
  need(!target.observe(sample(1,0,9000,QueueRateScope::TargetPayload,9),0),"Independent target payload baseline");
  need(global.observe(sample(2,1000,16000,QueueRateScope::OwnerCombined),1)==0,"Current source bucket excluded");
  need(target.observe(sample(2,1000,12000,QueueRateScope::TargetPayload,9),1)==0,"Target current bucket excluded");
  need(global.observe(sample(3,2000,22000,QueueRateScope::OwnerCombined),2)==200,"Actual combined send source average");
  need(target.observe(sample(3,2000,15000,QueueRateScope::TargetPayload,9),2)==100,"Actual payload send source average");
  auto absent=sample(4,3000,0,QueueRateScope::TargetPayload,9);absent.receivedBytes.reset();need(!target.observe(absent,3),"Missing send is unknown");
  need(!target.observe(sample(5,4000,1000000,QueueRateScope::TargetPayload,10),4),"Reopened admission cannot inherit rate or gap bytes");
  need(!global.observe(sample(4,3000,30000,QueueRateScope::TargetPayload),3),"Scope switch rebaselines rather than reusing global authority");
}
void equalMillisRetainsAcceptedHistoryAndPendingDelta() {
  QueueSendRateSmoothing target;
  need(!target.observe(sample(1,0,0,QueueRateScope::TargetPayload),0),"Actual send baseline");
  need(target.observe(sample(2,1000,3000,QueueRateScope::TargetPayload),1)==0,"Current bucket excluded");
  need(target.observe(sample(3,2000,6000,QueueRateScope::TargetPayload),2)==100,"Actual positive source rate");
  need(target.observe(sample(4,2000,6000,QueueRateScope::TargetPayload),2)==100,"New sequence in same millisecond retains positive history");
  need(target.observe(sample(5,2000,9000,QueueRateScope::TargetPayload),2)==100,"Same tick retains uncharged actual bytes");
  need(!target.observe(sample(4,2000,6000,QueueRateScope::TargetPayload),2),"Stale sequence remains rejected without resetting history");
  need(target.observe(sample(6,3000,12000,QueueRateScope::TargetPayload),3)==200,"Next genuine tick excludes its current bucket");
  need(target.observe(sample(7,4000,12000,QueueRateScope::TargetPayload),4)==400,"Pending same-millisecond bytes are charged exactly once");
  need(!target.observe(sample(8,3999,12000,QueueRateScope::TargetPayload),4),"Actual clock regression still withdraws history");
  QueueGlobalReceiveAverage global;
  need(!global.observe(sample(1,0,0)),"Actual combined baseline");
  need(global.observe(sample(2,1500,1536))==1,"Actual combined source rate");
  need(global.observe(sample(3,1500,3072))==1,"Equal-millisecond combined observation keeps the current rate");
  need(global.observe(sample(4,3000,4608))==1,"Next actual combined interval includes pending bytes once");
  QueueSmoothedEta cached;
  need(cached.observe(eta(1,1000,1000,10000,9000,100))==100,"Actual ETA facts");
  need(cached.observe(eta(2,1000,1000,1,1,100))==100,"Equal-millisecond accepted ETA keeps the original cache");
}
void etaSourceRemainingCacheAndZeroRate() {
  QueueSmoothedEta source;
  need(source.observe(eta(1, 0, 1000, 10000, 9000, 100)) == 100, "Max of actual remaining facts");
  need(source.observe(eta(2, 500, 1500, 1, 1, 100)) == 100, "Original900ms cache");
  need(source.observe(eta(3, 900, 1900, 1, 1, 100)) == 100, "Exactly900 retains source cache");
  need(source.observe(eta(4, 901, 1901, 1, 1, 100)) == 1, "Positive unfinished ETA never flickers0");
  need(source.observe(eta(5, 1802, 2802, 1, 1, 0)) == QueueSmoothedEta::InfiniteSeconds,
       "Known zero smoothed rate uses actual Java constant");
  auto done = eta(6, 2803, 3803, 0, 0, 0); done.startedMonotonicMillis = 0; done.finishedMonotonicMillis = 2200;
  done.smoothedPayloadBytesPerSecond.reset();
  need(source.observe(done) == -2, "Actual completed duration is signed, not an unknown rate");
}
void etaUnknownCompletionAndExtremes() {
  QueueSmoothedEta source;
  auto value = eta(1, 0, 1000, 10, 20, 1); value.smoothedPayloadBytesPerSecond.reset();
  need(!source.observe(value), "No instantaneous ETA fallback");
  need(source.observe(eta(2, 1000, 2000, 1, std::numeric_limits<std::int64_t>::max(), 1))
      == std::numeric_limits<std::int64_t>::max(), "Integer quotient does not overflow or clamp to infinity");
  value = eta(3, 2000, 3000, 0, 0, 1); value.startedMonotonicMillis = 0; value.finishedMonotonicMillis = 2001;
  need(!source.observe(value), "Future completion timestamp rejected");
  value = eta(4, 3000, 4000, 0, 0, 1); value.startedMonotonicMillis = 2000; value.finishedMonotonicMillis = 3000;
  need(source.observe(value) == 0, "Short actual completion");
  need(!source.observe(eta(5, 4000, 3999, 100, 100, 1)), "Wall reset clears source cache");
  need(source.observe(eta(6, 5000, 6000, 100, 100, 1)) == 100, "Fresh complete facts after reset");
  need(!source.observe(eta(7, 9001, 10001, 100, 100, 1)), "Long monotonic gap is unknown");
}
}
int main() {
  try {
    for (auto method : {globalSourceWarmupAndNominalConversion, globalBoundedHistoryAndExtremes,
        globalUnknownScopeResetAndStale, globalDiscontinuities, payloadUsesSourceWindowAndNotInstantRate,
        smoothingOldIntervalAndPendingDelta, smoothingGapsAndBounds, sendChannelsKeepActualGlobalCombinedAndTargetPayloadOwnership, equalMillisRetainsAcceptedHistoryAndPendingDelta, etaSourceRemainingCacheAndZeroRate,
        etaUnknownCompletionAndExtremes}) { method(); ++families; }
    std::cout << "PASS " << families << " queue-rate-provider families; " << assertions
              << " assertions; explicit primitives only, no sampler or whole-scheduler equality\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
