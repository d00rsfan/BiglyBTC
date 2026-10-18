#pragma once

#include "biglybtc/column_layout.hpp"

#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>

namespace biglybtc::ui {

enum class ColumnLayoutReadState { Ready, Missing, Failed };
enum class ColumnLayoutReadSource { Primary, Backup, Saving };
struct ColumnLayoutsPacket {
  quint64 session = 0;
  quint64 request = 0;
  ColumnLayoutReadState state = ColumnLayoutReadState::Failed;
  std::optional<ColumnLayoutReadSource> source{};
  std::shared_ptr<const core::ColumnLayouts> snapshot{};
  QString failure;
};
using ColumnLayoutsPresentation = std::shared_ptr<const ColumnLayoutsPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::ColumnLayoutsPresentation)
