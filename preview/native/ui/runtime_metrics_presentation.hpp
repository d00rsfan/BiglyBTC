#pragma once

#include "biglybtc/runtime_metrics.hpp"

#include <QMetaType>
#include <memory>

namespace biglybtc::ui {
struct RuntimeMetricsPacket {
  quint64 session = 0;
  quint64 epoch = 0;
  quint64 sequence = 0; // Owner publication sequence; distinct from snapshot response sequence.
  std::shared_ptr<const core::RuntimeMetrics> snapshot{};
  bool current = true;
};
using RuntimeMetricsPresentation = std::shared_ptr<const RuntimeMetricsPacket>;
} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::RuntimeMetricsPresentation)
