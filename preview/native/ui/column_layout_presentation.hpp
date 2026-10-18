#pragma once

#include "biglybtc/column_layout.hpp"
#include "biglybtc/classic_preferences.hpp"

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
  std::optional<core::ClassicPreferences> nativePreferences{};
};
using ColumnLayoutsPresentation = std::shared_ptr<const ColumnLayoutsPacket>;

// Immutable header capture; all file baselines remain worker-owned.
struct ColumnLayoutSaveRequest {
  quint64 session = 0;
  quint64 request = 0;
  std::shared_ptr<const core::ClassicPreferences> preferences;
};
using ColumnLayoutSaveSubmission = std::shared_ptr<const ColumnLayoutSaveRequest>;
struct ColumnLayoutSavePacket {
  quint64 session = 0;
  quint64 request = 0;
  bool saved = false;
  QString failure;
};
using ColumnLayoutSavePresentation = std::shared_ptr<const ColumnLayoutSavePacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::ColumnLayoutsPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::ColumnLayoutSaveSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::ColumnLayoutSavePresentation)
