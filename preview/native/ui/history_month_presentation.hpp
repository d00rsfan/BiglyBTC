#pragma once

#include "history_presentation.hpp"

#include <biglybtc/saved_history_month.hpp>

namespace biglybtc::ui {

enum class HistoryMonthReadState { Ready, Failed };
// Immutable GUI result of 30 independent reads, not a transactional snapshot.
struct HistoryMonthPacket {
  quint64 session = 0, request = 0;
  core::HistoryDate endingDate;
  HistoryMonthReadState state = HistoryMonthReadState::Failed;
  std::shared_ptr<const core::SavedHistoryMonth> snapshot;
  QString failure;
};
using HistoryMonthPresentation = std::shared_ptr<const HistoryMonthPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::HistoryMonthPresentation)
