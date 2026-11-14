class QToolButton;
#pragma once
#include "file_merge_presentation.hpp"
class QProgressBar;
#include "i2p_presentation.hpp"
#include "chat_page.hpp"
#include "swarm_discoveries_page.hpp"
#include "file_operation_presentation.hpp"
#include "recheck_presentation.hpp"
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
#include "publication_presentation.hpp"
#include "column_layout_presentation.hpp"
#include "statistics_presentation.hpp"
#include "overall_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "all_peers_presentation.hpp"
#include "torrent_detail_pages.hpp"
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

#include "../app/manual_tag_commands.hpp"
#include "../app/category_commands.hpp"
#include "../app/download_history_commands.hpp"
#include "rss_sources_presentation.hpp"
#include "metasearch_presentation.hpp"
#include "subscriptions_presentation.hpp"
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
struct LibraryChatNotifications;

class FileOperationDialog;
class RecheckDialog;
class TorrentCreationDialog;
class OptionsDialog;
class SchedulerDialog;
class LocalRatingDialog;
class ManualTagManagerDialog;
class CategoryManagerDialog;
class ClassicColumns;
class StatisticsPage;
class AllPeersPage;
class DownloadHistoryPage;
class RssSourcesPage;
class MetasearchPage;
class SubscriptionsPage;
class EngineActivityHistory;
class NativeActivityHistory;

class TorrentIntakeDialog;
class PublicationControls;
class PublicationReviewDialog;
class SavedServicePage;
class MetasearchMagnetDialog;

class ClassicStatusBar;
class LibraryWindow final : public QMainWindow {
  Q_OBJECT
public:
  LibraryWindow();
  void setChatSnapshot(ChatViewSnapshot);
  void showPublicChat(QString);
  void joinPublicChat(QString);void openExternalChatLink(QUrl);
  void setDownloadChatSnapshot(quint64,QString,ChatViewSnapshot);
  void setSwarmSnapshot(SwarmViewSnapshot);
  void chatSendFinished(QString key,QString text,bool accepted);
  ~LibraryWindow()override;
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
  PublicationControls* publicationControls(PublicationKind)const;
  void setPublicationCatalog(PublicationCatalogPresentation);
  void setPublicationResult(PublicationPresentation);
  void rejectPublicationCatalog(PublicationCatalogKey);
  void rejectPublication(const PublicationCommand&);
  void rejectPublicationConfirmation(PublicationConfirmation);
  void setSavedServices(SavedServicesPresentation);
  StatisticsPage* statisticsPage() const;
  DownloadHistoryPage* downloadHistoryPage()const;
  SubscriptionsPage* subscriptionsPage()const;
  void setSubscriptionCatalog(SubscriptionCatalogPresentation);
  void setSubscriptionResult(SubscriptionPresentation);
  void rejectSubscriptionCatalog(SubscriptionCatalogKey);
  void rejectSubscriptionCommand(const SubscriptionCommand&);
  void subscriptionHistoryChanged(quint64 session);
  MetasearchPage* metasearchPage()const;
  void setMetasearchCatalog(MetasearchCatalogPresentation);
  void setMetasearchResult(MetasearchPresentation);
  void setMetasearchDownloadResult(MetasearchDownloadPresentation);
  void rejectMetasearchCatalog(MetasearchCatalogKey);
  void rejectMetasearchSearch(const MetasearchQueryCommand&);
  void rejectMetasearchDownload(const MetasearchDownloadCommand&);
  void rejectMetasearchMagnet(const MetasearchMagnetSubmission&);
  RssSourcesPage* rssSourcesPage()const;
  void setRssCatalog(RssCatalogPresentation);
  void setRssFetchResult(RssFetchPresentation);
  void rejectRssCatalog(RssCatalogRequest);
  void rejectRssFetch(const RssFetchCommand&);
  void setDownloadHistoryCommandFinished(DownloadHistoryCommandPresentation);
  void rejectDownloadHistoryCommand(const DownloadHistoryCommand&);
  void setStatistics(StatisticsPresentation);
  void setHistoryRevision(quint64 session, quint64 revision);
  void setHistory(HistoryPresentation);
  void setHistoryWeek(HistoryWeekPresentation);
  void setHistoryMonth(HistoryMonthPresentation);
  void setHistoryRange(HistoryRangePresentation);
  void setEngineStatistics(EngineStatisticsPresentation);
  void setNativeSession(NativeSessionPresentation);
  void setI2p(I2pPresentation);
  void setOverall(OverallPresentation);
  void setFileMerge(FileMergePresentation);
  void setRuntimeMetrics(RuntimeMetricsPresentation);
  void setPeersSnapshot(PeerPresentation);
  void setTorrentDetailSnapshot(TorrentDetailPresentation);
  void setTorrentOptionsFinished(quint64 session,quint64 request,QString failure);
  void setTorrentTrackerFinished(quint64 session,quint64 request,QString failure);
  void setAzureusStatistics(AzureusStatisticsPresentation);
  void setLiveSpeedManager(LiveSpeedManagerPresentation);
  LiveSpeedManagerPresentation liveSpeedManagerSnapshot() const;
  void setManualTagStatistics(ManualTagStatisticsPresentation);
  void setManualTagCommandFinished(ManualTagCommandPresentation);
  void rejectManualTagCommand(const ManualTagCommand&);
  void setCategoryCommandFinished(CategoryCommandPresentation);
  void rejectCategoryCommand(const CategoryCommand&);
  void setCountryStatistics(CountryStatisticsPresentation);
  AzureusStatisticsPresentation azureusStatisticsSnapshot() const { return latestAzureus_; }
  CountryStatisticsPresentation countryStatisticsSnapshot() const { return latestCountry_; }
  ManualTagStatisticsPresentation manualTagStatisticsSnapshot() const { return latestManualTags_; }
  NativeSessionPresentation nativeSessionSnapshot() const { return latestNativeSession_; }
  NativeActivityPresentation nativeActivitySnapshot() const { return latestNativeActivity_; }
  void enableSessionCloseHandshake() { sessionCloseEnabled_ = true; }
  void completeSessionClose();
  void offerRecoveryClose(quint64 session);
  void retrySessionClose() { sessionCloseRequested_ = false; closing_ = false; updateActions(); }
  void beginColumnLayoutSession(quint64 session);
  void setColumnLayouts(ColumnLayoutsPresentation);
  void setColumnLayoutSaved(ColumnLayoutSavePresentation);
  void setDistributedRatings(DistributedRatingsPresentation);
  void setLocalRating(LocalRatingPresentation);
  void setLocalRatingSaved(LocalRatingSavePresentation);
  void setQuickRatingSaved(LocalRatingSavePresentation);
  void setRatingSortSaved(quint64 session,bool personal,QString failure);
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
  void setRecheckProgress(RecheckPresentation);
  void rejectRecheck(RecheckKey);
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
  void ratingSortRequested(quint64 session,bool personal);
  void swarmRatingRequested(quint64,QByteArray);
 void swarmSearchRequested(QString);void swarmCancelSearchRequested();
  void swarmRefreshRequested();void swarmDiscoverRequested();void swarmLookupRequested(QByteArray);
  void swarmSettingsRequested(biglybtc::services::related_content::Settings);
  void swarmReadRequested(QStringList,bool);void swarmRemoveRequested(QStringList);
  void swarmResetDeletedRequested();void swarmLayoutRequested(QByteArray);
  void downloadChatRequested(quint64,QString);void downloadChatObservationChanged(quint64,QString);
  void chatJoinRequested(QString);void chatSelectRequested(QString);void chatLeaveRequested(QString);
  void chatReadingChanged(QString);
  void chatSendRequested(QString,QString);void chatOptionsRequested(QString,bool,bool);
  void chatIndicationsRequested(QString,quint64,bool,bool);
  void chatNotificationSettingsRequested(biglybtc::services::msgsync::ChatNotificationSettings,biglybtc::services::msgsync::ChatNotificationSettings);
  void chatNotificationSoundRequested(QString);void chatNotificationReset();
  void chatDisplayPreferenceRequested(QString,bool,bool,bool);void chatParticipantPreferenceRequested(QString,QByteArray,bool,bool,bool);
  void chatNicknameModeRequested(QString,bool);void chatNicknameRequested(QString,QString,bool);
  void manualTagCommandRequested(biglybtc::ui::ManualTagCommand);
  void categoryCommandRequested(biglybtc::ui::CategoryCommand);
  void downloadHistoryCommandRequested(biglybtc::ui::DownloadHistoryCommand);
  void subscriptionCatalogRequested(biglybtc::ui::SubscriptionCatalogKey);
  void subscriptionCommandRequested(biglybtc::ui::SubscriptionCommand);
  void subscriptionCancelRequested(biglybtc::ui::SubscriptionCommandKey);
  void metasearchCatalogRequested(biglybtc::ui::MetasearchCatalogKey);
  void metasearchRequested(biglybtc::ui::MetasearchQueryCommand);
  void metasearchCancelRequested(biglybtc::ui::MetasearchQueryKey);
  void metasearchDownloadRequested(biglybtc::ui::MetasearchDownloadCommand);
  void metasearchDownloadCancelRequested(biglybtc::ui::MetasearchDownloadKey);
  void metasearchMagnetConfirmed(biglybtc::ui::MetasearchMagnetSubmission);
  void rssCatalogRequested(biglybtc::ui::RssCatalogRequest);
  void rssFetchRequested(biglybtc::ui::RssFetchCommand);
  void rssCancelRequested(biglybtc::ui::RssFetchKey);
  void fileOperationSequenceFinished(biglybtc::ui::FileOperationSequencePresentation);
  void fileOperationPlanRequested(biglybtc::ui::FileOperationRequest);
  void recheckRequested(biglybtc::ui::RecheckRequest);
  void recheckCancelRequested(biglybtc::ui::RecheckKey);
  void fileOperationExecuteRequested(biglybtc::ui::FileOperationSubmission);
  void fileOperationCancelRequested(biglybtc::ui::FileOperationRequest);
  void torrentCreateRequested(biglybtc::ui::TorrentCreationRequest);
  void torrentCreateCancelRequested(biglybtc::ui::TorrentCreationRequest);
  void localRatingRequested(biglybtc::ui::LocalRatingRequest);
  void localRatingSaveRequested(biglybtc::ui::LocalRatingSubmission);
  void quickRatingRequested(biglybtc::ui::QuickRatingSubmission);
  void schedulerRequested(biglybtc::ui::SchedulerRequest);
  void schedulerSaveRequested(biglybtc::ui::SchedulerSubmission);
  void optionsRequested(biglybtc::ui::OptionsRequest);
  void optionsSaveRequested(biglybtc::ui::OptionsSubmission);
  void refreshRequested();
  void torrentImportRequested(QString metadata, QString destination, bool start);
  void torrentIntakeRequested(biglybtc::ui::TorrentIntakeRequest);
  void torrentIntakeSubmitted(biglybtc::ui::TorrentIntakeSubmission);
  void magnetImportRequested(QString uri, QString destination);
  void publicationCatalogRequested(biglybtc::ui::PublicationCatalogKey);
  void publicationRequested(biglybtc::ui::PublicationCommand);
  void publicationConfirmed(biglybtc::ui::PublicationConfirmation);
  void publicationCancelRequested(biglybtc::ui::PublicationCommandKey);
  void savedServicesRequested(quint64 request);
  void statisticsRequested(quint64 session, quint64 request);
  void peersObservationChanged(bool active);
  void torrentDetailObservationChanged(biglybtc::ui::TorrentDetailInterest);
  void torrentOptionsRequested(biglybtc::ui::TorrentOptionsCommand);
  void torrentTrackerRequested(biglybtc::ui::TorrentTrackerCommand);
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
  void sessionCloseWithoutCheckpointRequested(quint64 session);
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
  void requestRecheck();
  std::optional<DownloadTargets> recheckTargets()const;
  void cancelRecheckView();
  void bindDownloadMenu(QMenu*);
  void openCategories();
  bool sendCategoryCommand(CategoryCommand);
  void rebuildCategoryMenu(QMenu*);
  void openManualTags();
  bool sendManualTagCommand(ManualTagCommand);
  void rebuildManualTagMenu(QMenu*);
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
  void ensureTorrentIntakeDialog();
  void openTorrentIntake();
  void restoreSelection(const QStringList&, bool initial = false);
  void activate(QTableView*);
  void showMyTorrents();
  void showMyTracker();
  void showMyShares();
  void showStatistics();
  void showAllPeers();
  void showSwarmDiscoveries();
  QPointer<SwarmDiscoveriesPage> swarmPage_;SwarmViewSnapshot swarmSnapshot_;
  void showChats();
  QPointer<ChatPage> chatPage_;
  ChatViewSnapshot chatSnapshot_;
  ClassicStatusBar* classicStatus_=nullptr;
  QToolButton* chatIndicator_=nullptr;
  QToolButton* azureusIndicator_=nullptr;
  QString chatReadingContext_,pendingChatSelection_;
  void updateChatReadContext(bool inactive=false);
  void updateChatIndicator();void tickChatNotifications();void editChatNotificationSettings();
  std::unique_ptr<LibraryChatNotifications> chatNotifications_;
  void showDownloadHistory();
  void showSubscriptions();
  bool onSubscriptionsPage()const;
  void showRssSources();
  void showMetasearch();
  void showMetasearchMagnetReview(MetasearchDownloadPresentation);
  void invalidateMetasearchMagnetReview();
  bool onMetasearchPage()const;
  bool onRssSourcesPage()const;
  bool onDownloadHistoryPage()const;
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
  void installPublicationControls(PublicationKind,SavedServicePage*);
  void refreshPublicationViews();
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
  QPointer<RecheckDialog> recheckDialog_;
  std::optional<RecheckRequest> activeRecheck_;
  quint64 recheckSequence_=0;
  bool recheckCancelSent_=false;
  QPointer<QAction> forceRecheck_;
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
  void openLocalRatingTarget(Target);
  bool quickLocalRating(QTableView*,const QModelIndex&,int);
  std::optional<LocalRatingRequest> quickRatingPending_;
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
  bool torrentContextActive() const;
  void placeDetailTabs();
  void openTorrentDetails(const Target&,int subtab=-1);
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
  QPointer<PublicationControls> sharingControls_,trackerControls_;
  QPointer<PublicationReviewDialog> publicationReview_;
  QPointer<SavedSharesPage> sharesPage_;
  QPointer<StatisticsPage> statisticsPage_;
  QPointer<SubscriptionsPage> subscriptionsPage_;
  QAction* subscriptionsAction_=nullptr;
  QPointer<RssSourcesPage> rssPage_;
  QPointer<MetasearchPage> metasearchPage_;
  QAction* metasearchAction_=nullptr;
  QAction* rssAction_=nullptr;
  QPointer<DownloadHistoryPage> downloadHistoryPage_;
  QAction* downloadHistoryAction_=nullptr;
  quint64 downloadHistoryRequest_=0;
  std::optional<DownloadHistoryCommand> pendingDownloadHistoryCommand_;
  I2pPresentation latestI2p_;
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
  bool ratingPersonalSort_=false;
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
  QSplitter* detailsHome_=nullptr;
  QProgressBar* startupProgress_=nullptr;
  bool inlineDetails_=false,detailsOnDoubleClick_=true;
  bool confirmExit_=false,exitPromptOpen_=false;
  std::size_t activeChecks_=0;
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
  std::array<QPointer<QAction>, core::DownloadActionCount> downloadActions_;
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
  quint64 downloadMenuSession_ = 0;
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
  MetasearchDownloadPresentation pendingMetasearchReview_;
  std::optional<MetasearchDownloadKey> metasearchReview_,metasearchMagnetReview_;
  QPointer<MetasearchMagnetDialog> metasearchMagnetDialog_;
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
  TorrentDetailPages* torrentDetailPages_=nullptr;TorrentDetailInterest torrentDetailInterest_;TorrentDetailPresentation latestTorrentDetail_;quint64 detailFilesLayoutSession_=0;quint64 torrentOptionsRequest_=0,torrentOptionsPending_=0,torrentTrackerRequest_=0,torrentTrackerPending_=0;
  void updateTorrentDetailInterest();
  FileMergePresentation latestFileMerge_;
  QPointer<AllPeersPage> peersPage_;QAction* allPeersAction_=nullptr;bool peersObserved_=false;
  void updatePeersVisibility();
  AzureusStatisticsPresentation latestAzureus_{};
  LiveSpeedManagerPresentation latestLiveSpeed_{};
  ManualTagStatisticsPresentation latestManualTags_{};
  QPointer<QAction> categoryAction_;
  QPointer<CategoryManagerDialog> categoryDialog_;
  CategoryCommandPresentation latestCategoryEditor_;
  std::optional<CategoryCommand> pendingCategoryCommand_;
  quint64 categoryRequest_=0;
  QPointer<QAction> manualTagAction_;
  QPointer<ManualTagManagerDialog> manualTagDialog_;
  ManualTagCommandPresentation latestManualTagEditor_;
  std::optional<ManualTagCommand> pendingManualTagCommand_;
  quint64 manualTagRequest_=0;
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
  quint64 contextSession_ = 0;
};
} // namespace biglybtc::ui
