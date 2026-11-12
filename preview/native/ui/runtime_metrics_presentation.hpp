#pragma once

#include "biglybtc/runtime_metrics.hpp"
#include "network_identity.hpp"
#include "biglybtc/tracker_statistics.hpp"
#include "azureus_statistics_presentation.hpp"

#include <QMetaType>
#include <memory>

namespace biglybtc::ui {
struct RuntimeMetricsPacket {
  quint64 session = 0;
  quint64 epoch = 0;
  quint64 sequence = 0; // Owner publication sequence; distinct from snapshot response sequence.
  std::shared_ptr<const core::RuntimeMetrics> snapshot{};
  bool current = true;
  bool mainlineObserved=false;int mainlineState=0;
  quint64 mainlineLookups=0,mainlineAnnounces=0,mainlinePeers=0;
  std::shared_ptr<const core::TrackerStatistics> tracker{};
  std::shared_ptr<const services::network_identity::Input> networkInput;
  std::shared_ptr<const services::network_identity::Snapshot> network;
};
using RuntimeMetricsPresentation = std::shared_ptr<const RuntimeMetricsPacket>;
} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::RuntimeMetricsPresentation)
