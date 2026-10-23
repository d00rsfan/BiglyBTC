#pragma once

#include "biglybtc/saved_statistics.hpp"
#include "saved_speed_manager_presentation.hpp"

#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>

namespace biglybtc::ui {

enum class StatisticsReadState { Ready, Missing, Failed };
enum class StatisticsReadSource { Primary, Backup, Saving };
struct StatisticsPacket {
  quint64 session = 0;
  quint64 request = 0;
  StatisticsReadState state = StatisticsReadState::Failed;
  std::optional<StatisticsReadSource> source{};
  std::shared_ptr<const core::SavedStatistics> snapshot{};
  QString failure;
  SavedSpeedManagerPresentation savedSpeed{};
};
using StatisticsPresentation = std::shared_ptr<const StatisticsPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::StatisticsPresentation)
