#pragma once
#include "scheduler.hpp"
#include <QMetaType>
#include <QString>
#include <memory>

namespace biglybtc::ui {
namespace schedule = plugins::speed_scheduler;
struct SchedulerRequest { quint64 session=0, request=0; };
struct SchedulerPreferences {
  bool enabled=false, useTags=false, overrideForced=false;
  int pollMilliseconds=10000;
  bool operator==(const SchedulerPreferences&) const = default;
};
struct SchedulerInterruptedAction {
  QString hash, action;
  bool imported=false;
  QString identity; // Opaque bounded source token; never displayed.
};
struct SchedulerValues {
  SchedulerPreferences preferences;
  schedule::Configuration configuration;
  bool hasSavedSchedules=false, inheritedOwnershipUnverified=false, interruptedAction=false;
  QString directory, diagnostic;
  std::optional<SchedulerInterruptedAction> interrupted{};
};
struct SchedulerPacket {
  quint64 session=0, request=0, revision=0;
  std::shared_ptr<const SchedulerValues> snapshot;
  QString failure;
};
using SchedulerPresentation=std::shared_ptr<const SchedulerPacket>;
struct SchedulerEditedSchedule { std::optional<std::size_t> sourceIndex; schedule::Schedule value; };
struct SchedulerSubmission {
  quint64 session=0, request=0, revision=0;
  SchedulerPreferences preferences;
  int defaultUpload=0, defaultDownload=0;
  bool enforceDefaults=true;
  std::vector<SchedulerEditedSchedule> schedules;
  // A separate explicit operation; cannot be combined with schedule edits.
  std::optional<QString> settleInterruptedHash{};
  QString settleInterruptedIdentity{};
};
struct SchedulerSettlementResult { QString hash; bool settled=false; QString failure; };
struct SchedulerSavePacket {
  quint64 session=0, request=0, revision=0;
  bool saved=false, schedulesSaved=false;
  SchedulerPresentation snapshot;
  QString failure;
  std::optional<SchedulerSettlementResult> settlement{};
};
using SchedulerSavePresentation=std::shared_ptr<const SchedulerSavePacket>;
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::SchedulerRequest)
Q_DECLARE_METATYPE(biglybtc::ui::SchedulerSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::SchedulerPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::SchedulerSavePresentation)
