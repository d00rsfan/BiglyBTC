#pragma once
#include "file_operation_presentation.hpp"
#include "file_operation_sequence.hpp"
#include "options_presentation.hpp"
#include "scheduler_presentation.hpp"
#include "local_rating_presentation.hpp"

#include "native_session_presentation.hpp"
#include "native_activity_presentation.hpp"

#include "library_model.hpp"
#include "file_model.hpp"
#include "torrent_intake_presentation.hpp"
#include "saved_services_presentation.hpp"
#include "column_layout_presentation.hpp"
#include "statistics_presentation.hpp"
#include "overall_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "azureus_statistics_presentation.hpp"
#include "live_speed_manager_presentation.hpp"
#include "manual_tag_presentation.hpp"
#include "country_statistics_presentation.hpp"
#include "statistics_navigation.hpp"
#include "history_presentation.hpp"
#include "history_week_presentation.hpp"
#include "history_month_presentation.hpp"
#include "history_range_presentation.hpp"
#include "engine_statistics_presentation.hpp"

#include <QMainWindow>
#include <QPointer>

#include <array>

class QAction;
class QLabel;
class QSortFilterProxyModel;
class QTableView;
class QTabWidget;
class QLineEdit;
class QSplitter;
class QEvent;
class QMenu;

namespace biglybtc::ui {

class FileOperationDialog;
class TorrentCreationDialog;
class OptionsDialog;
class SchedulerDialog;
class LocalRatingDialog;
class ClassicColumns;
class StatisticsPage;
class EngineActivityHistory;
class NativeActivityHistory;

class TorrentIntakeDialog;

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
  void setHistoryRevision(quint64 session, quint64 revision);
  void setHistory(HistoryPresentation);
  void setHistoryWeek(HistoryWeekPresentation);
  void setHistoryMonth(HistoryMonthPresentation);
  void setHistoryRange(HistoryRangePresentation);
  void setEngineStatistics(EngineStatisticsPresentation);
  void setNativeSession(NativeSessionPresentation);
  void setOverall(OverallPresentation);
  void setRuntimeMetrics(RuntimeMetricsPresentation);
  void setAzureusStatistics(AzureusStatisticsPresentation);
  void setLiveSpeedManager(LiveSpeedManagerPresentation);
  LiveSpeedManagerPresentation liveSpeedManagerSnapshot() const;
  void setManualTagStatistics(ManualTagStatisticsPresentation);
  void setCountryStatistics(CountryStatisticsPresentation);
  AzureusStatisticsPresentation azureusStatisticsSnapshot() const { return latestAzureus_; }
  CountryStatisticsPresentation countryStatisticsSnapshot() const { return latestCountry_; }
  ManualTagStatisticsPresentation manualTagStatisticsSnapshot() const { return latestManualTags_; }
  NativeSessionPresentation nativeSessionSnapshot() const { return latestNativeSession_; }
  NativeActivityPresentation nativeActivitySnapshot() const { return latestNativeActivity_; }
  void enableSessionCloseHandshake() { sessionCloseEnabled_ = true; }
  void completeSessionClose();
  void retrySessionClose() { sessionCloseRequested_ = false; closing_ = false; updateActions(); }
  void beginColumnLayoutSession(quint64 session);
  void setColumnLayouts(ColumnLayoutsPresentation);
  void setColumnLayoutSaved(ColumnLayoutSavePresentation);
  void setLocalRating(LocalRatingPresentation);
  void setLocalRatingSaved(LocalRatingSavePresentation);
  void rejectLocalRatingRequest(const LocalRatingRequest&);
  void rejectLocalRatingSave(const LocalRatingSubmission&);
  void setScheduler(SchedulerPresentation);
  void setSchedulerSaved(SchedulerSavePresentation);
  void rejectSchedulerRequest(SchedulerRequest);
  void rejectSchedulerSave(const SchedulerSubmission&);
  void setOptions(OptionsPresentation);
  void setOptionsSaved(OptionsSavePresentation);
  void rejectOptionsRequest(OptionsRequest);
  void rejectOptionsSave(const OptionsSubmission&);
  void setFileOperationPlan(FileOperationPlanPresentation);
  void setFileOperationResult(FileOperationResultPresentation);
  void setTorrentCreationProgress(TorrentCreationProgressPresentation);
  void setTorrentCreationResult(TorrentCreationResultPresentation);
  void rejectFileOperationRequest(FileOperationRequest);
  void rejectFileOperationSubmission(const FileOperationSubmission&);
  void rejectTorrentCreationRequest(const TorrentCreationRequest&);
  void setIntakeDefaults(quint64 session, QString directory);
  void setTorrentIntake(TorrentIntakePresentation);
  void setTorrentIntakeFinished(TorrentIntakeResultPresentation);
  void rejectTorrentIntake(const TorrentIntakeRequest&);
  void rejectTorrentIntakeSubmission(const TorrentIntakeSubmission&);
  void setBusy(bool);
  void setMessage(const QString&);
  void setOfflinePreview(bool);
  // Syntactic UI admission only; the Host owns metadata/URI validation and IO.
  bool submitTorrentImport(QString metadata, QString destination, bool start);
  bool submitMagnetImport(QString uri, QString destination);
signals:
  void fileOperationSequenceFinished(biglybtc::ui::FileOperationSequencePresentation);
  void fileOperationPlanRequested(biglybtc::ui::FileOperationRequest);
  void fileOperationExecuteRequested(biglybtc::ui::FileOperationSubmission);
  void fileOperationCancelRequested(biglybtc::ui::FileOperationRequest);
  void torrentCreateRequested(biglybtc::ui::TorrentCreationRequest);
  void torrentCreateCancelRequested(biglybtc::ui::TorrentCreationRequest);
  void localRatingRequested(biglybtc::ui::LocalRatingRequest);
  void localRatingSaveRequested(biglybtc::ui::LocalRatingSubmission);
  void schedulerRequested(biglybtc::ui::SchedulerRequest);
  void schedulerSaveRequested(biglybtc::ui::SchedulerSubmission);
  void optionsRequested(biglybtc::ui::OptionsRequest);
  void optionsSaveRequested(biglybtc::ui::OptionsSubmission);
  void refreshRequested();
  void torrentImportRequested(QString metadata, QString destination, bool start);
  void torrentIntakeRequested(biglybtc::ui::TorrentIntakeRequest);
  void torrentIntakeSubmitted(biglybtc::ui::TorrentIntakeSubmission);
  void magnetImportRequested(QString uri, QString destination);
  void savedServicesRequested(quint64 request);
  void statisticsRequested(quint64 session, quint64 request);
  void manualTagStatisticsRequested(quint64 session);
  void countryStatisticsRequested(quint64 session);
  void historyRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate date);
  void historyWeekRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate endingDate);
  void historyMonthRequested(quint64 session, quint64 request, biglybtc::core::HistoryDate endingDate);
  void historyRangeRequested(quint64 session, quint64 request, biglybtc::core::HistoryRangeRequest);
  void columnLayoutsRequested(quint64 session, quint64 request);
  void columnLayoutSaveRequested(biglybtc::ui::ColumnLayoutSaveSubmission);
  void continueRequested(biglybtc::ui::Target);
  void downloadActionRequested(biglybtc::ui::DownloadTargets, biglybtc::core::DownloadAction);
  void hybridCompanionRequested(biglybtc::ui::Target source, bool selectV2);
  void filePolicyRequested(biglybtc::ui::Target, biglybtc::ui::FileIndices, int priority, bool skipped);
  void fileCompactionRequested(biglybtc::ui::Target, int originalIndex);
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
  std::optional<DownloadTargets> selectedDownloadTargets() const;
  std::optional<DownloadTargets> downloadTargets(core::DownloadAction, bool all) const;
  void dispatchDownload(core::DownloadAction, bool all = false);
  void dispatchDownloadAlias(bool pauseResume);
  void bindDownloadMenu(QMenu*);
  void queueDownloadActions();
  void updateDownloadActions();
  struct FilePolicySelection {
    Target target;
    FileIndices indices;
    FilePresentation source;
    bool operator==(const FilePolicySelection&) const = default;
  };
  std::optional<FilePolicySelection> filePolicySelection(bool forCompaction = false) const;
  void dispatchFileCompaction();
  void dispatchFilePolicy(int priority, bool skipped);
  void filePopup(const QPoint&);
  bool canImport() const;
  void openImportDialog(bool magnet);
  void openTorrentIntake();
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
  void queueLiveHistoryRefresh();
  void queueOptionalViews();
  void queueManualTagStatistics();
  void queueCountryStatistics();
  void closePage(int index);
  bool onSavedServicePage() const;
  bool hasLoadingServices(quint64 request) const;
  void refreshSavedServices();
  void queueSavedServices();
  void failSavedServices(quint64 request, const QString& message);
  void refreshColumnLayouts(bool initial = false);
  void queueColumnLayouts();
  void saveColumnLayout();
  void openFileOperation(FileOperation,int originalIndex=-1,QString destination={});
  void openFileMapping(FileOperation);
  void openTorrentCreation();
  QPointer<FileOperationDialog> fileOperationDialog_;
  QPointer<FileOperationSequence> fileOperationSequence_;
  QPointer<TorrentCreationDialog> torrentCreationDialog_;
  std::array<QPointer<QAction>,4> fileOperationActions_;
  QPointer<QAction> reviewReplacement_;
  QPointer<QAction> createTorrentAction_;
  std::optional<FileOperationRequest> activeFileOperation_;
  bool fileOperationExecuting_ = false;
  std::optional<TorrentCreationRequest> activeTorrentCreation_;
  quint64 creationDialogRevision_ = 0;
  void requestHybridCompanion(bool selectV2);
  std::array<QPointer<QAction>,2> hybridCompanionActions_{};
  void openLocalRating();
  QPointer<LocalRatingDialog> localRatingDialog_;
  QPointer<QAction> localRatingAction_;
  std::optional<LocalRatingRequest> localRatingIdentity_;
  quint64 localRatingRequest_=0;
  bool localRatingLoading_=false, localRatingSaving_=false;
  void openOptions();
  void openScheduler();
  void queueScheduler();
  void queueOptions();
  void updateGeneral();
  void updateCounts();
  void updateActions();
  void selectionChanged();
  void requestDetails(bool force = false);
  void popup(QTableView*, const QPoint&);
  QAction* schedulerAction_ = nullptr;
  QPointer<SchedulerDialog> schedulerDialog_;
  std::optional<SchedulerRequest> pendingSchedulerLoad_, activeSchedulerLoad_;
  std::optional<SchedulerSubmission> activeSchedulerSave_;
  bool schedulerDispatchQueued_=false;
  QAction* optionsAction_ = nullptr;
  QPointer<OptionsDialog> optionsDialog_;
  std::optional<OptionsRequest> pendingOptionsLoad_, activeOptionsLoad_;
  std::optional<OptionsSubmission> activeOptionsSave_;
  bool optionsDispatchQueued_ = false;
  QString defaultSaveDirectory_;
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
  quint64 savedHistoryRevision_ = 0;
  std::array<quint64, 4> requestedHistoryRevisions_{};
  bool liveHistoryRefreshQueued_ = false;
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
  std::array<QPointer<QAction>, 10> downloadActions_;
  QPointer<QAction> startStop_;
  QPointer<QAction> pauseResume_;
  QPointer<QAction> startAll_;
  QPointer<QAction> stopAll_;
  std::optional<core::DownloadAction> startStopChoice_;
  std::optional<core::DownloadAction> pauseResumeChoice_;
  bool offlinePreview_ = false;
  bool downloadDispatching_ = false;
  bool downloadActionsQueued_ = false;
  quint64 downloadActionRevision_ = 0;
  quint64 renderedDownloadRevision_ = 0;
  quint64 downloadMenuToken_ = 0;
  bool downloadMenuOpen_ = false;
  quint64 downloadMenuRevision_ = 0;
  std::optional<DownloadTargets> downloadMenuTargets_;
  QPointer<QAction> openTorrent_;
  QPointer<QAction> openMagnet_;
  std::array<QPointer<QAction>, 4> filePolicyActions_;
  QPointer<QAction> fileCompaction_;
  std::array<QPointer<QAction>,4> filePathActions_;
  bool fileMenuOpen_ = false;
  std::optional<FilePolicySelection> fileMenuSelection_;
  std::optional<Target> detailsRequestedFor_;
  std::optional<int> restoreFileIndex_;
  QPointer<TorrentIntakeDialog> intakeDialog_;
  std::optional<TorrentIntakeRequest> activeIntakePreview_;
  std::optional<TorrentIntakeSubmission> activeIntakeSubmission_;
  bool busy_ = false;
  bool closing_ = false;
  bool sessionCloseEnabled_ = false;
  bool sessionCloseRequested_ = false;
  bool sessionCloseAcknowledged_ = false;
  NativeSessionPresentation latestNativeSession_{};
  OverallPresentation latestOverall_{};
  RuntimeMetricsPresentation latestRuntime_{};
  AzureusStatisticsPresentation latestAzureus_{};
  LiveSpeedManagerPresentation latestLiveSpeed_{};
  ManualTagStatisticsPresentation latestManualTags_{};
  QPointer<StatisticsPage> pendingManualTagRead_;
  bool manualTagDispatchQueued_ = false;
  CountryStatisticsPresentation latestCountry_{};
  QPointer<StatisticsPage> pendingCountryRead_;
  bool countryDispatchQueued_ = false;
  bool suppressAutomaticDetails_ = false;
  bool updating_ = false;
  bool presented_ = false;
  bool contextOpen_ = false;
  std::optional<Target> contextTarget_;
};
} // namespace biglybtc::ui
