#pragma once
#include "biglybtc/country_statistics.hpp"
#include <QMetaType>
#include <memory>

namespace biglybtc::ui {
struct CountryStatisticsPacket {
  quint64 session = 0, epoch = 0, sequence = 0;
  std::shared_ptr<const core::CountryStatistics> snapshot;
  bool current = true;
};
using CountryStatisticsPresentation = std::shared_ptr<const CountryStatisticsPacket>;
// Publication and ledger sequences have different domains. Provider changes may
// replace a snapshot at the same ledger revision, without changing peer data.
inline bool acceptCountryStatistics(const CountryStatisticsPresentation& previous,
    const CountryStatisticsPresentation& incoming, quint64 expectedSession) noexcept {
  if (!incoming || !expectedSession || incoming->session != expectedSession || !incoming->sequence) { return false; }
  if (incoming->snapshot) {
    const auto& value = *incoming->snapshot;
    if (!incoming->epoch || !core::validCountryStatistics(value)
        || value.identity.profileSession != expectedSession || value.identity.engineEpoch != incoming->epoch) { return false; }
  } else if (incoming->epoch) { return false; }
  if (!previous) { return true; }
  if (previous->session != expectedSession || incoming->sequence <= previous->sequence || incoming->epoch < previous->epoch) { return false; }
  if (previous->snapshot && incoming->snapshot) {
    const auto& old = *previous->snapshot; const auto& value = *incoming->snapshot;
    if (value.identity.applicationRun != old.identity.applicationRun
        || (incoming->epoch == previous->epoch && value.sequence < old.sequence)) { return false; }
  }
  return true;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::CountryStatisticsPresentation)
