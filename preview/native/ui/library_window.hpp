#pragma once

#include "native_session_presentation.hpp"
#include "native_activity_presentation.hpp"

#include "library_model.hpp"
#include "file_model.hpp"
#include "saved_services_presentation.hpp"
#include "column_layout_presentation.hpp"
#include "statistics_presentation.hpp"
#include "overall_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "statistics_navigation.hpp"
#include "history_presentation.hpp"
#include "history_week_presentation.hpp"
#include "history_month_presentation.hpp"
#include "history_range_presentation.hpp"
#include "engine_statistics_presentation.hpp"

#include <QMainWindow>
#include <QPointer>

class QAction;
class QLabel;
class QSortFilterProxyModel;
class QTableView;
class QTabWidget;
class QLineEdit;
class QSplitter;
class QEvent;

namespace biglybtc::ui {

class ClassicColumns;
class StatisticsPage;
class EngineActivityHistory;
class NativeActivityHistory;

class LibraryWindow final : public QMainWindow {
  Q_OBJECT
public:
  LibraryWindow();
  LibraryModel& model() { return *model_; }
  QTableView& table() { return *activeTable_; }
  QTableView& incompleteTable() { return *incompleteTable_; }
  QTableView& completeTable() { return *completeTable_; }
  QTabWidget& mainTabs() { return *mainTabs_; }
  QTabWidget& detailTabs() { return *detailTabs_; }
  QLineEdit& filter() { return *filter_; }
  QSplitter& splitter() { return *splitter_; }
  QAction& continueAction() { return *continue_; }
  QAction& cancelAction() { return *cancel_; }
  QAction& filesAction() { return *files_; }
  QAction& paddingAction() { return *padding_; }
  FileModel& filesModel() { return *filesModel_; }
  QTableView& filesTable() { return *filesTable_; }
  void setPresentation(const Presentation&);
  void setTelemetry(const Telemetry&);
  void setDetails(const Details&);
  SavedHostingPage* hostingPage() const { return hostingPage_.data(); }
  SavedSharesPage* sharesPage() const { return sharesPage_.data(); }
  void setSavedServices(SavedServicesPresentation);
  StatisticsPage* statisticsPage() const;
  void setStatistics(StatisticsPresentation);
  void setHistory(HistoryPresentation);
  void setHistoryWeek(HistoryWeekPresentation);
  void setHistoryMonth(HistoryMonthPresentation);
  void setHistoryRange(HistoryRangePresentation);
  void setEngineStatistics(EngineStatisticsPresentation);
  void setNativeSession(NativeSessionPresentation);
  void setOverall(OverallPresentation);
  void setRuntimeMetrics(RuntimeMetricsPresentation);
  NativeSessionPresentation nativeSessionSnapshot() const { return latestNativeSession_; }
  NativeActivityPresentation nativeActivitySnapshot() const { return latestNativeActivity_; }
  void enableSessionCloseHandshake() { sessionCloseEnabled_ = true; }
  void completeSessionClose();
  void retrySessionClose() { sessionCloseRequested_ = false; closing_ = false; updateActions(); }
  void beginColumnLayoutSession(quint64 session);
  void setColumnLayouts(ColumnLayoutsPresentation);
  void setColumnLayoutSaved(ColumnLayoutSavePresentation);
  void setBusy(bool);
  void setMessage(const QString&);
  void setOfflinePreview(bool);
signals:
  void refreshRequested();
  void savedServicesRequested(quint64 request);
  void statisticsRequested(quint64 session, quint64 request);
  void historyRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate date);
  void historyWeekRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate endingDate);
  void historyMonthRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate endingDate);
  void historyRangeRequested(quint64 session, quint64 request, biglybtc::core::HistoryRangeRequest);
  void columnLayoutsRequested(quint64 session, quint64 request);
  void columnLayoutSaveRequested(biglybtc::ui::ColumnLayoutSaveSubmission);
  void continueRequested(biglybtc::ui::Target);
  void detailsRequested(biglybtc::ui::Target);
  void cancelRequested();
  void sessionCloseRequested();
  void markRequested(quint64 session, bool clear);
protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  std::optional<Target> selected() const;
  std::optional<Target> actionTarget() const;
  QStringList selectedHashes() const;
  void restoreSelection(const QStringList&, bool initial = false);
  void activate(QTableView*);
  void showMyTorrents();
  void showMyTracker();
  void showMyShares();
  void showStatistics();
  bool onStatisticsPage() const;
  void refreshStatistics();
  void refreshHistory(core::HistoryDate);
  void refreshHistoryWeek(core::HistoryDate);
  void refreshHistoryMonth(core::HistoryDate);
  void refreshHistoryRange(core::HistoryRangeRequest);
  void queueStatistics();
  void queueOptionalViews();
  void closePage(int index);
  bool onSavedServicePage() const;
  bool hasLoadingServices(quint64 request) const;
  void refreshSavedServices();
  void queueSavedServices();
  void failSavedServices(quint64 request, const QString& message);
  void refreshColumnLayouts(bool initial = false);
  void queueColumnLayouts();
  void saveColumnLayout();
  void updateGeneral();
  void updateCounts();
  void updateActions();
  void selectionChanged();
  void requestDetails(bool force = false);
  void popup(QTableView*, const QPoint&);
  LibraryModel* model_;
  QTableView* incompleteTable_;
  QTableView* completeTable_;
  QTableView* activeTable_;
  QTabWidget* mainTabs_;
  QWidget* torrentsPage_;
  QPointer<SavedHostingPage> hostingPage_;
  QPointer<SavedSharesPage> sharesPage_;
  QPointer<StatisticsPage> statisticsPage_;
  struct StatisticsNavigationRecord {
    quint64 session = 0;
    StatisticsNavigation navigation{};
  };
  // Closed-tab memory contains only scalar choices, never widgets or results.
  std::optional<StatisticsNavigationRecord> rememberedStatisticsNavigation_;
  struct StatisticsRequest {
    quint64 session = 0;
    quint64 request = 0;
    QPointer<StatisticsPage> page;
    std::optional<core::HistoryDate> historyDate{};
    bool historyWeek = false;
    bool historyMonth = false;
    std::optional<core::HistoryRangeRequest> range{};
  };
  quint64 statisticsRequest_ = 0;
  quint64 historyRequest_ = 0;
  std::optional<StatisticsRequest> pendingHistoryRequest_;
  std::optional<StatisticsRequest> activeStatisticsRequest_;
  std::optional<StatisticsRequest> pendingStatisticsRequest_;
  bool statisticsDispatchQueued_ = false;
  quint64 savedRequest_ = 0;
  std::optional<quint64> activeSavedRequest_;
  std::optional<quint64> pendingSavedRequest_;
  bool savedDispatchQueued_ = false;
  struct ColumnRequest {
    quint64 session = 0;
    quint64 request = 0;
    std::optional<quint64> incompleteRevision;
    std::optional<quint64> completeRevision;
  };
  QPointer<ClassicColumns> incompleteColumns_;
  QPointer<ClassicColumns> completeColumns_;
  EngineStatisticsPresentation latestEngineStatistics_;
  std::shared_ptr<EngineActivityHistory> engineActivity_;
  std::shared_ptr<NativeActivityHistory> nativeActivity_;
  NativeActivityPresentation latestNativeActivity_;
  quint64 nativeApplicationRun_ = 0;
  quint64 columnSession_ = 0;
  quint64 columnRequest_ = 0;
  std::optional<ColumnRequest> activeColumnRequest_;
  std::optional<ColumnRequest> pendingColumnRequest_;
  bool columnDispatchQueued_ = false;
  bool columnSaveAvailable_ = false;
  quint64 columnSaveRequest_ = 0;
  std::optional<ColumnRequest> activeColumnSave_;

  QTabWidget* detailTabs_;
  QLineEdit* filter_;
  QSplitter* splitter_;
  QLabel* incompleteCount_;
  QLabel* completeCount_;
  QLabel* generalName_;
  QLabel* generalState_;
  QLabel* generalSize_;
  QLabel* generalProgress_;
  QLabel* generalReceived_;
  QLabel* generalHash_;
  QLabel* previewStatus_;
  QLabel* message_;
  FileModel* filesModel_;
  QSortFilterProxyModel* filesProxy_;
  QTableView* filesTable_;
  QAction* closeTab_;
  QAction* reloadColumns_;
  QAction* saveColumns_;
  QAction* continue_;
  QAction* cancel_;
  QAction* refresh_;
  QAction* files_;
  QAction* padding_;
  std::optional<Target> detailsRequestedFor_;
  std::optional<int> restoreFileIndex_;
  bool busy_ = false;
  bool closing_ = false;
  bool sessionCloseEnabled_ = false;
  bool sessionCloseRequested_ = false;
  bool sessionCloseAcknowledged_ = false;
  NativeSessionPresentation latestNativeSession_{};
  OverallPresentation latestOverall_{};
  RuntimeMetricsPresentation latestRuntime_{};
  bool suppressAutomaticDetails_ = false;
  bool updating_ = false;
  bool presented_ = false;
  bool contextOpen_ = false;
  std::optional<Target> contextTarget_;
};
} // namespace biglybtc::ui
