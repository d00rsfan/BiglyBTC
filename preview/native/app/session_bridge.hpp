#pragma once
#include "file_operation_presentation.hpp"
#include "owned_i2p_service.hpp"
#include "i2p_presentation.hpp"
#include "options_presentation.hpp"
#include "scheduler_presentation.hpp"
#include "local_rating_presentation.hpp"
#include "torrent_intake_presentation.hpp"
#include "rss_sources_presentation.hpp"

#include "biglybtc/executor.hpp"
#include "column_layout_presentation.hpp"
#include "library_model.hpp"
#include "saved_services_presentation.hpp"
#include "statistics_presentation.hpp"
#include "history_presentation.hpp"
#include "history_week_presentation.hpp"
#include "history_month_presentation.hpp"
#include "history_range_presentation.hpp"
#include "engine_statistics_presentation.hpp"
#include "native_session_presentation.hpp"
#include "overall_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "manual_tag_presentation.hpp"
#include "manual_tag_commands.hpp"
#include "category_commands.hpp"
#include "download_history_commands.hpp"
#include "country_statistics_presentation.hpp"
#include "live_speed_manager_presentation.hpp"

#include <QObject>

#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>

class QTimer;

namespace biglybtc::engine { struct ApplicationPeerObservation; }
namespace biglybtc::search { class RssHttpFetch; }
namespace biglybtc::storage { class ProfileLease; class CountryProvider; class SchedulerService; }

namespace biglybtc::app {

class OwnedAzureusService;

class SessionBridge final : public QObject {
  Q_OBJECT
public:
  using CountryProviderFactory = std::function<std::unique_ptr<storage::CountryProvider>(const storage::ProfileLease&)>;
  using AzureusServiceFactory = std::function<std::unique_ptr<OwnedAzureusService>(const storage::ProfileLease&)>;
  using SchedulerFactory = std::function<std::unique_ptr<storage::SchedulerService>(const storage::ProfileLease&)>;
  using RssFetchFactory=std::function<std::unique_ptr<search::RssHttpFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::RssSourceKey&,std::stop_token)>;
  explicit SessionBridge(QObject* parent = nullptr, CountryProviderFactory = {}, AzureusServiceFactory = {}, SchedulerFactory = {}, std::optional<I2pServiceOptions> = {}, RssFetchFactory = {});
  ~SessionBridge() override;
  bool open(std::filesystem::path profile, unsigned short ownedPeerPort);
  bool openNormal(std::filesystem::path profile);
  bool downloadAction(ui::DownloadTargets, core::DownloadAction);
  bool torrentImport(QString metadata, QString destination, bool start);
  bool createHybridCompanion(ui::Target source, bool selectV2);
  bool previewTorrentIntake(ui::TorrentIntakeRequest);
  bool importTorrentFiles(ui::TorrentIntakeSubmission);
  bool magnetImport(QString uri, QString destination);
  bool filePolicy(ui::Target, ui::FileIndices, int priority, bool skipped);
  bool compactFile(ui::Target, int originalIndex);
  bool loadLocalRating(ui::LocalRatingRequest);
  bool saveLocalRating(ui::LocalRatingSubmission);
  bool loadScheduler(ui::SchedulerRequest);
  bool saveScheduler(ui::SchedulerSubmission);
  bool loadOptions(ui::OptionsRequest);
  bool saveOptions(ui::OptionsSubmission);
  bool prepareFileOperation(ui::FileOperationRequest);
  bool executeFileOperation(ui::FileOperationSubmission);
  void cancelFileOperation(ui::FileOperationRequest);
  bool createTorrent(ui::TorrentCreationRequest);
  void cancelTorrentCreation(ui::TorrentCreationRequest);
  bool refresh();
  bool savedServices(quint64 request);
  bool statistics(quint64 session, quint64 request);
  bool markStatistics(quint64 session, bool clear);
  bool manualTagStatistics(quint64 session);
  bool manualTagCommand(ui::ManualTagCommand);
  bool categoryCommand(ui::CategoryCommand);
  bool downloadHistoryCommand(ui::DownloadHistoryCommand);
  bool rssCatalog(ui::RssCatalogRequest);
  bool rssFetch(ui::RssFetchCommand);
  void cancelRssFetch(ui::RssFetchKey);
  bool countryStatistics(quint64 session);
  bool history(quint64 session, quint64 request, core::HistoryDate date);
  bool historyWeek(quint64 session, quint64 request, core::HistoryDate endingDate);
  bool historyMonth(quint64 session, quint64 request, core::HistoryDate endingDate);
  bool historyRange(quint64 session, quint64 request, core::HistoryRangeRequest);
  bool columnLayouts(quint64 session, quint64 request);
  bool saveColumnLayout(ui::ColumnLayoutSaveSubmission);
  bool details(ui::Target target);
  bool continueDownload(ui::Target target);
  void cancel();
  bool closeSession();
  bool closeSessionWithoutCheckpoint(quint64 session);
  bool busy() const { return busy_; }
signals:
  void fileOperationPrepared(biglybtc::ui::FileOperationPlanPresentation);
  void fileOperationFinished(biglybtc::ui::FileOperationResultPresentation);
  void torrentCreationProgress(biglybtc::ui::TorrentCreationProgressPresentation);
  void torrentCreated(biglybtc::ui::TorrentCreationResultPresentation);
  void torrentIntakeReady(biglybtc::ui::TorrentIntakePresentation);
  void torrentIntakeFinished(biglybtc::ui::TorrentIntakeResultPresentation);
  void localRatingReady(biglybtc::ui::LocalRatingPresentation);
  void localRatingSaved(biglybtc::ui::LocalRatingSavePresentation);
  void schedulerReady(biglybtc::ui::SchedulerPresentation);
  void schedulerSaved(biglybtc::ui::SchedulerSavePresentation);
  void optionsReady(biglybtc::ui::OptionsPresentation);
  void optionsSaved(biglybtc::ui::OptionsSavePresentation);
  void intakeDefaultsReady(quint64 session, QString directory);
  void snapshotReady(biglybtc::ui::Presentation);
  void i2pReady(biglybtc::ui::I2pPresentation);
  void progressReady(biglybtc::ui::Telemetry);
  void detailsReady(biglybtc::ui::Details);
  void savedServicesReady(biglybtc::ui::SavedServicesPresentation);
  void statisticsReady(biglybtc::ui::StatisticsPresentation);
  // Actual successful saved-history mutations only, scoped to one profile.
  void historyChanged(quint64 session, quint64 revision);
  void historyReady(biglybtc::ui::HistoryPresentation);
  void historyWeekReady(biglybtc::ui::HistoryWeekPresentation);
  void historyMonthReady(biglybtc::ui::HistoryMonthPresentation);
  void historyRangeReady(biglybtc::ui::HistoryRangePresentation);
  void engineStatisticsReady(biglybtc::ui::EngineStatisticsPresentation);
  void columnLayoutSessionReady(quint64 session);
  void columnLayoutsReady(biglybtc::ui::ColumnLayoutsPresentation);
  void columnLayoutSaved(biglybtc::ui::ColumnLayoutSavePresentation);
  void nativeSessionReady(biglybtc::ui::NativeSessionPresentation);
  void overallReady(biglybtc::ui::OverallPresentation);
  void runtimeMetricsReady(biglybtc::ui::RuntimeMetricsPresentation);
  void azureusStatisticsReady(biglybtc::ui::AzureusStatisticsPresentation);
  void liveSpeedManagerReady(biglybtc::ui::LiveSpeedManagerPresentation);
  void manualTagStatisticsReady(biglybtc::ui::ManualTagStatisticsPresentation);
  void manualTagCommandFinished(biglybtc::ui::ManualTagCommandPresentation);
  void categoryCommandFinished(biglybtc::ui::CategoryCommandPresentation);
  void downloadHistoryCommandFinished(biglybtc::ui::DownloadHistoryCommandPresentation);
  void rssCatalogReady(biglybtc::ui::RssCatalogPresentation);
  void rssFetchReady(biglybtc::ui::RssFetchPresentation);
  void countryStatisticsReady(biglybtc::ui::CountryStatisticsPresentation);
  void sessionClosed(quint64 session, bool joined, bool storageReleased);
  void sessionCloseFailed();
  void recoveryCloseOffered(quint64 session);
  void sessionClosedWithoutCheckpoint(quint64 session,bool joined,bool storageReleased);
  void busyChanged(bool);
  void messageReady(QString);
private:
  struct Host;
  bool dispatch(std::move_only_function<void(std::stop_token)> operation, bool recoverLibrarySnapshot = true, bool closingOperation = false);
  void creationProgress(ui::TorrentCreationProgressPresentation);
  void ready(ui::Presentation snapshot, QString message = {});
  void failed(QString message);
  void progress(ui::Telemetry update);
  void engineStatistics(ui::EngineStatisticsPresentation);
  void nativeSession(ui::NativeSessionPresentation);
  void ensureScheduler();
  void tickScheduler() noexcept;
  void initializePersistentOwner(std::stop_token);
  void publishMainlineFailure() noexcept;
  void publishI2p();
  void reportIdlePollFailure(quint64 session,QString detail);
  void publishDownloadHistoryCompletionNotice(QString);
  void observeNative(const engine::ApplicationPeerObservation&) noexcept;
  void refreshDhtAccounting(bool ending = false) noexcept;
  void tickAccounting(bool force = false, bool ending = false) noexcept;
  void publishOverall(bool closed = false);
  void publishHistoryChange() noexcept;
  void publishRuntimeMetrics(const engine::ApplicationPeerObservation&);
  void publishAzureus() noexcept;
  void publishLiveSpeed() noexcept;
  ui::ManualTagStatisticsPresentation manualTagPacket();
  void publishManualTags() noexcept;
  void deliverManualTags(ui::ManualTagStatisticsPresentation);
  void deliverManualTagCommand(ui::ManualTagCommandPresentation);
  void deliverCategoryCommand(ui::CategoryCommandPresentation);
  void deliverDownloadHistoryCommand(ui::DownloadHistoryCommandPresentation);
  ui::CountryStatisticsPresentation countryPacket();
  void publishCountries() noexcept;
  void deliverCountries(ui::CountryStatisticsPresentation);
  void closeFailed(QString);
  void deliverRssCatalog(ui::RssCatalogPresentation);
  void deliverRssFetch(ui::RssFetchPresentation);
  void postRssFetch(ui::RssFetchPresentation);
  void finishRssDispatch();
  void rssDispatchInterrupted();
  void stopRssReceiver()noexcept;
  void closeWorkerRss();
  void pollRssFetch();
  bool closeSessionImpl(bool withoutCheckpoint,quint64 expectedSession);
  std::unique_ptr<Host> host_;
  std::optional<core::Ticket> current_;
  std::shared_ptr<std::atomic_bool> completionPosted_;
  QTimer* completionMonitor_ = nullptr;
  QTimer* idleMonitor_ = nullptr;
  std::optional<core::Ticket> idlePoll_;
  std::shared_ptr<std::atomic_bool> idlePollFailurePosted_;
  quint64 idlePollSession_=0,idleFailureSession_=0; // GUI-only diagnostic identities.
  QString idleFailureMessage_;
  bool closingSession_ = false;
  bool profileClosed_ = false; // GUI-only: closed admission differs from never-opened failure routing.
  std::mutex progressMutex_;
  std::map<QString, ui::Telemetry> latestProgress_;
  bool deliveryQueued_ = false;
  std::optional<ui::EngineStatisticsPresentation> latestEngineStatistics_;
  bool engineDeliveryQueued_ = false;
  std::optional<ui::NativeSessionPresentation> latestNativeSession_;
  bool nativeDeliveryQueued_ = false;
  std::optional<ui::OverallPresentation> latestOverall_;
  std::optional<ui::RuntimeMetricsPresentation> latestRuntime_;
  bool overallDeliveryQueued_ = false, runtimeDeliveryQueued_ = false;
  struct HistoryChange { quint64 session = 0, revision = 0; };
  std::optional<HistoryChange> latestHistoryChange_;
  bool historyChangeDeliveryQueued_ = false;
  std::optional<ui::ManualTagStatisticsPresentation> latestManualTags_;
  bool manualTagDeliveryQueued_ = false;
  ui::ManualTagStatisticsPresentation deliveredManualTags_; // GUI-only admission state.
  std::optional<ui::CategoryCommand> guiCategoryCommand_;
  std::optional<ui::DownloadHistoryCommand> guiDownloadHistoryCommand_;
  std::optional<ui::ManualTagCommand> guiManualTagCommand_; // One current async completion, GUI only.
  std::optional<ui::CountryStatisticsPresentation> latestCountries_;
  bool countryDeliveryQueued_ = false;
  ui::CountryStatisticsPresentation deliveredCountries_; // GUI-only admission state.
  CountryProviderFactory countryProviderFactory_; // Invoked only by the worker.
  AzureusServiceFactory azureusServiceFactory_; // Explicit owner policy; absent means disabled.
  std::optional<ui::AzureusStatisticsPresentation> latestAzureus_;
  bool azureusDeliveryQueued_ = false;
  std::optional<ui::LiveSpeedManagerPresentation> latestLiveSpeed_;
  bool liveSpeedDeliveryQueued_ = false;
  std::optional<I2pServiceOptions> i2pOptions_;
  SchedulerFactory schedulerFactory_;
  RssFetchFactory rssFetchFactory_;
  std::optional<ui::RssCatalogRequest> guiRssCatalog_;
  std::optional<ui::RssFetchCommand> guiRssFetch_;
  std::optional<ui::RssFetchKey> rssStarting_;
  std::optional<ui::FileOperationRequest> guiFileOperation_;
  std::optional<ui::TorrentCreationRequest> guiCreation_;
  ui::TorrentCreationProgressPresentation latestCreationProgress_;
  bool creationProgressQueued_ = false;
  bool busy_ = false;
  // GUI-thread publication only; worker commands capture the requested identity.
  quint64 publishedColumnLayoutSession_ = 0;
  // Destroy/join this worker before host/lease, tickets, mutex or QObject context.
  core::Executor worker_{8};
};
} // namespace biglybtc::app
