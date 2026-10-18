#pragma once

#include <biglybtc/saved_history.hpp>

#include <QMetaType>
#include <QString>

#include <memory>

namespace biglybtc::ui {

enum class HistoryReadState { Ready, Missing, Failed };
// Immutable GUI delivery; session/request/date identify one owned read.
struct HistoryPacket {
  quint64 session = 0, request = 0;
  core::HistoryDate date;
  HistoryReadState state = HistoryReadState::Failed;
  std::shared_ptr<const core::SavedHistoryDay> snapshot{};
  QString failure;
};
using HistoryPresentation = std::shared_ptr<const HistoryPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::core::HistoryDate)
Q_DECLARE_METATYPE(biglybtc::ui::HistoryPresentation)
