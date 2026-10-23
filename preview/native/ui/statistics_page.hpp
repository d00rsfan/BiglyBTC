#pragma once

#include "engine_activity_presentation.hpp"
#include "native_activity_presentation.hpp"
#include "native_session_presentation.hpp"
#include "engine_statistics_presentation.hpp"
#include "history_presentation.hpp"
#include "history_week_presentation.hpp"
#include "history_month_presentation.hpp"
#include "history_range_presentation.hpp"
#include "library_model.hpp"
#include "statistics_presentation.hpp"
#include "overall_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "azureus_statistics_presentation.hpp"
#include "manual_tag_presentation.hpp"
#include "country_statistics_presentation.hpp"
#include "statistics_navigation.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

#include <array>
#include <cstddef>
#include <optional>

class QLabel;
class QShowEvent;
class QTabWidget;

namespace biglybtc::ui {

class EngineActivityView;
class NativeSessionView;
class OverallView;
class RuntimeMetricsView;
class ManualTagStatisticsView;
class CountryStatisticsView;
class TrackerStatisticsView;
class AzureusStatisticsView;
class CacheStatisticsView;
class SavedSpeedManagerView;
class NativeActivityView;
class HistoryView;
class HistoryWeekView;
class HistoryMonthView;
class HistoryRangeView;

// GUI-thread only; completions are accepted once for the current nonzero pair.
// This read-only page neither performs IO nor starts transfers/sampling timers.
// Supplied packets and snapshots must remain immutable for their shared lifetime.
class StatisticsPage final : public QWidget {
  Q_OBJECT
public:
  static constexpr std::size_t MaximumSamples = 600;
  explicit StatisticsPage(QWidget* parent = nullptr, std::optional<StatisticsNavigation> initial = {});
  std::optional<StatisticsNavigation> navigation() const;
  void beginRequest(quint64 session, quint64 request);
  void setPresentation(StatisticsPresentation);
  void setFailed(quint64 session, quint64 request, const QString& message);
  void beginNativeSession(quint64 session);
  void setOverall(OverallPresentation);
  void setRuntimeMetrics(RuntimeMetricsPresentation);
  void setAzureusStatistics(AzureusStatisticsPresentation);
  void setManualTagStatistics(ManualTagStatisticsPresentation);
  ManualTagStatisticsPresentation manualTagStatisticsSnapshot() const;
  bool manualTagsSelected() const;
  void setCountryStatistics(CountryStatisticsPresentation);
  CountryStatisticsPresentation countryStatisticsSnapshot() const;
  bool countrySelected() const;
  void setAccountingBusy(bool);
  void setNativeActivity(NativeActivityPresentation);
  NativeSessionPresentation nativeSessionSnapshot() const;
  NativeActivityPresentation nativeActivitySnapshot() const { return nativeActivityFrame_; }
  void setEngineStatistics(EngineStatisticsPresentation);
  void setEngineActivity(EngineActivityPresentation);
  EngineActivityPresentation engineActivitySnapshot() const;
  void beginHistoryRequest(quint64 session, quint64 request, core::HistoryDate);
  void setHistory(HistoryPresentation);
  void setHistoryFailed(quint64 session, quint64 request, core::HistoryDate, const QString&);
  void resetHistorySession(quint64 session);
  core::HistoryDate selectedHistoryDate() const;
  HistoryPresentation historyPresentation() const;
  bool historySelected() const;
  void beginHistoryWeekRequest(quint64 session, quint64 request, core::HistoryDate);
  void setHistoryWeek(HistoryWeekPresentation);
  void setHistoryWeekFailed(quint64 session, quint64 request, core::HistoryDate, const QString&);
  core::HistoryDate selectedHistoryWeekDate() const;
  HistoryWeekPresentation historyWeekPresentation() const;
  bool historyWeekSelected() const;
  void beginHistoryMonthRequest(quint64 session, quint64 request, core::HistoryDate);
  void setHistoryMonth(HistoryMonthPresentation);
  void setHistoryMonthFailed(quint64 session, quint64 request, core::HistoryDate, const QString&);
  core::HistoryDate selectedHistoryMonthDate() const;
  HistoryMonthPresentation historyMonthPresentation() const;
  bool historyMonthSelected() const;
  void beginHistoryRangeRequest(quint64, quint64, core::HistoryRangeRequest);
  void setHistoryRange(HistoryRangePresentation);
  void setHistoryRangeFailed(quint64, quint64, core::HistoryRangeRequest, const QString&);
  HistoryRangePresentation historyRangePresentation() const;
  bool historyRangeSelected() const;
  void refreshHistoryRange();
  // Caller first admits generation+hash through LibraryModel. The page also
  // rejects invalid identity/negative payload telemetry and resets on target change.
  void recordPayload(const Telemetry&, const QString& displayName);
  QTabWidget& tabs();
  std::size_t sampleCount() const;
  std::optional<StatisticsReadState> readState() const { return state_; }
  bool loading() const { return loading_; }
  quint64 session() const { return session_; }
  quint64 request() const { return request_; }
signals:
  void markRequested(bool clear);
  void refreshRequested();
  void historyVisible();
  void manualTagsVisible();
  void countryVisible(bool activate = true);
  void manualTagStatisticsRequested(quint64 session);
  void historyRequested(biglybtc::core::HistoryDate);
  void historyWeekRequested(biglybtc::core::HistoryDate);
  void historyMonthRequested(biglybtc::core::HistoryDate);
  void historyRangeRequested(biglybtc::core::HistoryRangeRequest);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class ActivityPlot;
  struct Fact { QPointer<QLabel> label; QString text; QString tip; };
  using Facts = std::array<Fact, 18>; //13 raw,3 derived,status,provenance.
  Facts emptyFacts(QString status, QString provenance) const;
  bool writeFacts(const Facts&);
  bool accepts(quint64 session, quint64 request) const;
  void clearActivity();
  void refreshActivity();
  void refreshEngine();
  void refreshNative();
  void queueHistoryVisibility();
  QTabWidget* tabs_;
  QWidget* activity_;
  QTabWidget* activityViews_ = nullptr;
  EngineActivityView* engineActivity_ = nullptr;
  QPointer<OverallView> overallView_;
  std::array<QPointer<RuntimeMetricsView>, 4> runtimeViews_{};
  QPointer<ManualTagStatisticsView> manualTags_;
  QPointer<CountryStatisticsView> country_;
  bool countrySelectionPublished_ = false;
  QPointer<TrackerStatisticsView> trackers_;
  QPointer<CacheStatisticsView> cache_;
  QPointer<SavedSpeedManagerView> savedSpeed_;
  std::array<QPointer<AzureusStatisticsView>, 3> azureus_{};
  QPointer<NativeSessionView> nativeSessionView_;
  QPointer<NativeActivityView> nativeActivityView_;
  NativeActivityPresentation nativeActivityFrame_;
  quint64 nativeSessionBinding_ = 0, nativeApplicationRun_ = 0;
  ActivityPlot* plot_;
  QLabel* transfer_;
  QLabel* downloadRate_;
  QLabel* uploadRate_;
  QLabel* observations_;
  QLabel* status_;
  QLabel* provenance_;
  QTabWidget* transferTabs_ = nullptr;
  HistoryView* history_ = nullptr;
  QTabWidget* historyTabs_ = nullptr;
  HistoryWeekView* historyWeek_ = nullptr;
  HistoryMonthView* historyMonth_ = nullptr;
  HistoryRangeView* historyRange_ = nullptr;
  QWidget* engineDetails_ = nullptr;
  QLabel* engineStatus_ = nullptr;
  QLabel* engineTarget_ = nullptr;
  std::array<QLabel*, 12> engineValues_{};
  EngineStatisticsPresentation engine_{};
  bool engineDirty_ = true;
  std::array<QLabel*, 13> raw_{};
  std::array<QLabel*, 3> since_{};
  QPointer<QWidget> observedWindow_;
  quint64 session_ = 0;
  quint64 request_ = 0;
  bool loading_ = false;
  std::optional<StatisticsReadState> state_{};
  std::optional<Target> target_{};
  QString displayName_;
  QElapsedTimer clock_;
  quint64 activityRevision_ = 0;
  bool activityDirty_ = true;
  bool historyVisibilityQueued_ = false;
};

} // namespace biglybtc::ui
