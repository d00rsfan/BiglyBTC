#pragma once
#include "biglybtc/overall_statistics.hpp"
#include <QMetaType>
#include <QString>
#include <memory>
#include <optional>
namespace biglybtc::ui {
// Live accounting and last successfully saved values remain distinct.
struct OverallPacket {
  quint64 session = 0, sequence = 0;
  core::OverallValues values{};
  std::optional<core::OverallValues> saved{};
  bool available = false, dirty = false, writable = false, closed = false, finalCounters = false;
  QString failure;
  QString historyFailure;
};
using OverallPresentation = std::shared_ptr<const OverallPacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::OverallPresentation)
