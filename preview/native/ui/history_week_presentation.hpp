#pragma once

#include "history_presentation.hpp"

#include <biglybtc/saved_history_week.hpp>

namespace biglybtc::ui {

enum class HistoryWeekReadState { Ready, Failed };
// Immutable GUI result of seven independent reads, not a transactional snapshot.
struct HistoryWeekPacket {
  quint64 session = 0, request = 0;
  core::HistoryDate endingDate;
  HistoryWeekReadState state = HistoryWeekReadState::Failed;
  std::shared_ptr<const core::SavedHistoryWeek> snapshot;
  QString failure;
};
using HistoryWeekPresentation = std::shared_ptr<const HistoryWeekPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::HistoryWeekPresentation)
