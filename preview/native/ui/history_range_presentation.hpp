#pragma once

#include <biglybtc/saved_history_range.hpp>

#include <QMetaType>
#include <QString>

#include <memory>

namespace biglybtc::ui {

enum class HistoryRangeReadState { Ready, Failed };
// GUI-thread delivery of an independently owned, immutable saved query result.
struct HistoryRangePacket {
  quint64 session = 0, request = 0;
  core::HistoryRangeRequest query;
  HistoryRangeReadState state = HistoryRangeReadState::Failed;
  std::shared_ptr<const core::SavedHistoryRange> snapshot;
  QString failure;
};
using HistoryRangePresentation = std::shared_ptr<const HistoryRangePacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::core::HistoryRangeRequest)
Q_DECLARE_METATYPE(biglybtc::ui::HistoryRangePresentation)
