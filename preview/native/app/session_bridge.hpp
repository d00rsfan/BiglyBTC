#pragma once

#include "biglybtc/atomic_shared_ptr.hpp"
#include "local_media_controls.hpp"
#include "biglybtc/move_only_function.hpp"
#include "plugin_inventory.hpp"
#include "native_plugin_update_status.hpp"
#include <QByteArrayView>
#include "media_renderer_presentation.hpp"
#include "activity_centre.hpp"
#include "file_merge_presentation.hpp"
#include <string>
#include <utility>
#include <vector>
#include "file_operation_presentation.hpp"
#include "recheck_presentation.hpp"
#include "owned_i2p_service.hpp"
#include "i2p_presentation.hpp"
#include "options_presentation.hpp"
#include "scheduler_presentation.hpp"
#include "local_rating_presentation.hpp"
#include "torrent_intake_presentation.hpp"
#include "rss_sources_presentation.hpp"
#include "metasearch_presentation.hpp"
#include "subscriptions_presentation.hpp"

#include "biglybtc/executor.hpp"
#include "column_layout_presentation.hpp"
#include "library_model.hpp"
#include "saved_services_presentation.hpp"
#include "publication_presentation.hpp"
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
#include "tag_strip.hpp"
#include "download_history_commands.hpp"
#include "country_statistics_presentation.hpp"
#include "all_peers_presentation.hpp"
#include "all_trackers_view.hpp"
#include "torrent_detail_presentation.hpp"
#include "live_speed_manager_presentation.hpp"

#include <QObject>

#include <filesystem>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <stop_token>
#include <mutex>
#include <map>

class QTimer;

namespace biglybtc::engine { struct ApplicationPeerObservation; }
namespace biglybtc::media { struct MediaDeviceIdentity; struct FfmpegJob; }
namespace biglybtc::search {class MetasearchHttpFetch; class MetasearchMetainfoFetch; class SavedMetasearchService; class SavedMetasearchResultHandle; struct SavedSubscriptionResultSelector; class RssHttpFetch; class SavedSubscriptionService; class SubscriptionMetainfoFetch; }
namespace biglybtc::services::related_content {struct TagScanSelection;}
namespace biglybtc::storage { class ConfigFile; class ProfileLease; class CountryProvider; class SchedulerService; }

namespace biglybtc::ui {class LibraryWindow;}
namespace biglybtc::plugins::rpc { struct HttpCredentials; }

namespace biglybtc::storage { enum class RpcIdentityMember; }

namespace biglybtc::app {

class OwnedAzureusService;
struct NativePluginUpdateChannel;
class NetworkIdentityController;class GeneralChatController;class SwarmDiscoveryController;

class SessionBridge final : public QObject {
  Q_OBJECT
public:
  using CountryProviderFactory = std::function<std::unique_ptr<storage::CountryProvider>(const storage::ProfileLease&)>;
  using AzureusServiceFactory = std::function<std::unique_ptr<OwnedAzureusService>(const storage::ProfileLease&)>;
  using SchedulerFactory = std::function<std::unique_ptr<storage::SchedulerService>(const storage::ProfileLease&)>;
  using RssFetchFactory=std::function<std::unique_ptr<search::RssHttpFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::RssSourceKey&,std::stop_token)>;
  using SubscriptionFetchFactory=std::function<std::unique_ptr<search::RssHttpFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,std::stop_token)>;
  using SubscriptionAcquisitionFactory=std::function<std::unique_ptr<search::SubscriptionMetainfoFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,const QByteArray&,std::stop_token)>;
  using MetasearchFetchFactory=std::function<std::unique_ptr<search::MetasearchHttpFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::MetasearchEngineKey&,const QString&,std::stop_token)>;
  using MetasearchMetainfoFactory=std::function<std::unique_ptr<search::MetasearchMetainfoFetch>(const storage::ProfileLease&,const std::filesystem::path&,quint64,const search::SavedMetasearchResultHandle&,std::stop_token)>;
  explicit SessionBridge(QObject* parent = nullptr, CountryProviderFactory = {}, AzureusServiceFactory = {}, SchedulerFactory = {}, std::optional<I2pServiceOptions> = {}, RssFetchFactory = {}, SubscriptionFetchFactory = {}, SubscriptionAcquisitionFactory = {}, SubscriptionFetchFactory automaticFeed = {}, SubscriptionAcquisitionFactory automaticMetainfo = {}, MetasearchFetchFactory metasearch = {}, MetasearchMetainfoFactory metasearchMetainfo = {}, std::vector<std::pair<std::string,int>> mainlineBootstrap = {});
  ~SessionBridge() override;
  void connectChats(biglybtc::ui::LibraryWindow*);
  bool open(std::filesystem::path profile, unsigned short ownedPeerPort);
  bool openNormal(std::filesystem::path profile);
  void setPeersActive(bool active);
  void setTorrentDetailInterest(ui::TorrentDetailInterest);
  bool torrentOptions(ui::TorrentOptionsCommand);
  bool torrentTracker(ui::TorrentTrackerCommand);
  bool downloadAction(ui::DownloadTargets, core::DownloadAction);
  bool forceRecheck(ui::RecheckRequest);
  void cancelRecheck(ui::RecheckKey);
  bool torrentImport(QString metadata, QString destination, bool start);
  bool createHybridCompanion(ui::Target source, bool selectV2);
  bool previewTorrentIntake(ui::TorrentIntakeRequest);
  bool importTorrentFiles(ui::TorrentIntakeSubmission);
  bool magnetImport(QString uri, QString destination);
  void setDonationDialogOpen(bool open) { donationDialogOpen_.store(open); }
  bool updateDonationPreference(quint64 session, bool donated);
  bool filePolicy(ui::Target, ui::FileIndices, int priority, bool skipped);
  bool compactFile(ui::Target, int originalIndex);
  bool loadLocalRating(ui::LocalRatingRequest);
  bool saveLocalRating(ui::LocalRatingSubmission);
  bool quickLocalRating(ui::QuickRatingSubmission);
  bool setRatingPersonalSort(quint64 session,bool personal);
  bool loadScheduler(ui::SchedulerRequest);
  bool saveScheduler(ui::SchedulerSubmission);
  bool loadOptions(ui::OptionsRequest);
  bool loadJavaPluginInventory(quint64 session);
#ifndef _WIN32
  bool configureNativePluginUpdates(std::shared_ptr<const NativePluginUpdateChannel>);
  bool nativePluginUpdate(NativePluginRequest);
#endif
  bool saveOptions(ui::OptionsSubmission);
  bool prepareFileOperation(ui::FileOperationRequest);
  bool executeFileOperation(ui::FileOperationSubmission);
  void cancelFileOperation(ui::FileOperationRequest);
  bool createTorrent(ui::TorrentCreationRequest);
  void cancelTorrentCreation(ui::TorrentCreationRequest);
  bool refresh();
  bool savedServices(quint64 request);
  bool publicationCatalog(ui::PublicationCatalogKey);
  bool publicationCommand(ui::PublicationCommand);
  bool confirmPublication(ui::PublicationConfirmation);
  void cancelPublication(ui::PublicationCommandKey);
  bool statistics(quint64 session, quint64 request);
  bool markStatistics(quint64 session, bool clear);
  bool manualTagStatistics(quint64 session);
  bool manualTagCommand(ui::ManualTagCommand);
  bool allTrackers(quint64 session,quint64 request,bool apply,QStringList origins);
  bool categoryCommand(ui::CategoryCommand);
  bool activities(quint64,QString,QVector<ui::ActivityRefView>);
  bool activitySettings(quint64,ui::ChatNotificationSettings,ui::ChatNotificationSettings);
  bool downloadHistoryCommand(ui::DownloadHistoryCommand);
  bool subscriptionCatalog(ui::SubscriptionCatalogKey);
  bool subscriptionCommand(ui::SubscriptionCommand);
  void cancelSubscription(ui::SubscriptionCommandKey);
  bool metasearchCatalog(ui::MetasearchCatalogKey);
  bool metasearchQuery(ui::MetasearchQueryCommand);
  void cancelMetasearch(ui::MetasearchQueryKey);
  bool metasearchDownload(ui::MetasearchDownloadCommand);
  bool confirmMetasearchMagnet(ui::MetasearchMagnetSubmission);
  void cancelMetasearchDownload(ui::MetasearchDownloadKey);
  bool rssCatalog(ui::RssCatalogRequest);
  bool rssFetch(ui::RssFetchCommand);
  void cancelRssFetch(ui::RssFetchKey);
  bool countryStatistics(quint64 session);
  bool history(quint64 session, quint64 request, core::HistoryDate date);
  bool historyWeek(quint64 session, quint64 request, core::HistoryDate endingDate);
  bool historyMonth(quint64 session, quint64 request, core::HistoryDate endingDate);
  bool historyRange(quint64 session, quint64 request, core::HistoryRangeRequest);
  bool columnLayouts(quint64 session, quint64 request);
  bool assignTagChip(ui::TagChipAssignmentCommand);
  bool saveTagStripSettings(ui::TagStripSettingsCommand);
  bool saveColumnLayout(ui::ColumnLayoutSaveSubmission);
  bool details(ui::Target target);
  bool continueDownload(ui::Target target);
  bool startLocalMedia(media::MediaDeviceIdentity identity, quint16 port, bool discovery,
      std::optional<LocalMediaInterface> network = {});
  bool publishLocalMediaFile(ui::Target target, std::size_t file);
  bool stopLocalMedia();
  bool findLocalMediaRenderers();
  bool playLocalMediaOnRenderer(ui::Target, std::size_t, QUuid renderer,
      std::shared_ptr<const media::FfmpegJob> conversion = {});
  bool stopLocalMediaPlayback();
  bool startLocalRpc(quint16 port, QByteArrayView user, QByteArrayView password);
  bool startSavedLocalRpc(std::string pluginDirectory);
  bool stopLocalRpc();
  void cancel();
  bool closeSession();
  bool closeSessionWithoutCheckpoint(quint64 session);
  bool busy() const { return busy_; }
signals:
  void donationReminderReady(quint64 session);
  void startupProgressReady(quint64 session,quint64 done,quint64 total);
  void activitiesReady(biglybtc::ui::ActivityView);
  void fileOperationPrepared(biglybtc::ui::FileOperationPlanPresentation);
  void fileOperationFinished(biglybtc::ui::FileOperationResultPresentation);
  void recheckReady(biglybtc::ui::RecheckPresentation);
  void torrentCreationProgress(biglybtc::ui::TorrentCreationProgressPresentation);
  void torrentCreated(biglybtc::ui::TorrentCreationResultPresentation);
  void torrentIntakeReady(biglybtc::ui::TorrentIntakePresentation);
  void torrentIntakeFinished(biglybtc::ui::TorrentIntakeResultPresentation);
  void localRatingReady(biglybtc::ui::LocalRatingPresentation);
  void localRatingSaved(biglybtc::ui::LocalRatingSavePresentation);
  void quickRatingSaved(biglybtc::ui::LocalRatingSavePresentation);
  void ratingSortSaved(quint64 session,bool personal,QString failure);
  void schedulerReady(biglybtc::ui::SchedulerPresentation);
  void schedulerSaved(biglybtc::ui::SchedulerSavePresentation);
  void optionsReady(biglybtc::ui::OptionsPresentation);
  void javaPluginInventoryReady(biglybtc::ui::JavaPluginInventory);
  void nativePluginUpdateReady(biglybtc::app::NativePluginStatus);
  void optionsSaved(biglybtc::ui::OptionsSavePresentation);
  void intakeDefaultsReady(quint64 session, QString directory);
  void snapshotReady(biglybtc::ui::Presentation);
  void i2pReady(biglybtc::ui::I2pPresentation);
  void progressReady(biglybtc::ui::Telemetry);
  void detailsReady(biglybtc::ui::Details);
  void publicationCatalogReady(biglybtc::ui::PublicationCatalogPresentation);
  void publicationReady(biglybtc::ui::PublicationPresentation);
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
  void initialColumnLayoutsReady(biglybtc::ui::ColumnLayoutsPresentation);
  void tagChipAssignmentFinished(biglybtc::ui::TagChipAssignmentResult);
  void tagStripSettingsSaved(biglybtc::ui::TagStripSettingsResult);
  void columnLayoutSaved(biglybtc::ui::ColumnLayoutSavePresentation);
  void nativeSessionReady(biglybtc::ui::NativeSessionPresentation);
  void overallReady(biglybtc::ui::OverallPresentation);
  void fileMergeReady(biglybtc::ui::FileMergePresentation);
  void runtimeMetricsReady(biglybtc::ui::RuntimeMetricsPresentation);
  void peersReady(biglybtc::ui::PeerPresentation);
  void torrentDetailReady(biglybtc::ui::TorrentDetailPresentation);
  void torrentOptionsFinished(quint64 session,quint64 request,QString failure);
  void torrentTrackerFinished(quint64 session,quint64 request,QString failure);
  void azureusStatisticsReady(biglybtc::ui::AzureusStatisticsPresentation);
  void liveSpeedManagerReady(biglybtc::ui::LiveSpeedManagerPresentation);
  void manualTagStatisticsReady(biglybtc::ui::ManualTagStatisticsPresentation);
  void manualTagCommandFinished(biglybtc::ui::ManualTagCommandPresentation);
  void allTrackersReady(biglybtc::ui::AllTrackersPresentation);
  void swarmTagApplicationFinished(quint64 session,QString status);
  void categoryCommandFinished(biglybtc::ui::CategoryCommandPresentation);
  void downloadHistoryCommandFinished(biglybtc::ui::DownloadHistoryCommandPresentation);
  void subscriptionCatalogReady(biglybtc::ui::SubscriptionCatalogPresentation);
  void subscriptionReady(biglybtc::ui::SubscriptionPresentation);
  void subscriptionHistoryChanged(quint64 session);
  void metasearchCatalogReady(biglybtc::ui::MetasearchCatalogPresentation);
  void metasearchReady(biglybtc::ui::MetasearchPresentation);
  void metasearchDownloadReady(biglybtc::ui::MetasearchDownloadPresentation);
  void rssCatalogReady(biglybtc::ui::RssCatalogPresentation);
  void rssFetchReady(biglybtc::ui::RssFetchPresentation);
  void countryStatisticsReady(biglybtc::ui::CountryStatisticsPresentation);
  void localMediaReady(quint64 session, quint16 httpPort, quint16 udpPort, QByteArray publishedUrl);
  void mediaRenderersReady(quint64 session, bool scanning, biglybtc::ui::MediaRenderers);
  void mediaPlaybackReady(quint64 session, biglybtc::ui::MediaPlaybackState);
  void localRpcReady(quint64 session, quint16 port);
  void sessionClosed(quint64 session, bool joined, bool storageReleased);
  void sessionCloseWarning(QString failure);
  void sessionCloseFailed();
  void recoveryCloseOffered(quint64 session);
  void sessionClosedWithoutCheckpoint(quint64 session,bool joined,bool storageReleased);
  void busyChanged(bool);
  void messageReady(QString);
private:
  struct Host;
  GeneralChatController* chatController_=nullptr;
  SwarmDiscoveryController* swarmController_=nullptr;
  bool applyDiscoveredSwarmTags(quint64 session,std::vector<services::related_content::TagScanSelection>);
  bool dispatch(biglybtc::core::MoveOnlyFunction<void(std::stop_token)> operation, bool recoverLibrarySnapshot = true, bool closingOperation = false);
  void creationProgress(ui::TorrentCreationProgressPresentation);
  void ready(ui::Presentation snapshot, QString message = {});
  ui::ColumnLayoutsPresentation prepareColumnLayouts(quint64 request,std::stop_token);
  void failed(QString message);
  void progress(ui::Telemetry update);
  void engineStatistics(ui::EngineStatisticsPresentation);
  void nativeSession(ui::NativeSessionPresentation);
  void ensureScheduler();
  void tickScheduler() noexcept;
  void publishFileMerge();
  void initializePersistentOwner(std::stop_token);
  void publishMainlineFailure() noexcept;
  void publishI2p();
  void publishDonationReminder() noexcept;
  void reportIdlePollFailure(quint64 session,QString detail);
  void publishDownloadHistoryCompletionNotice(QString);
  NetworkIdentityController* networkIdentityController_=nullptr;
  void observeNative(const engine::ApplicationPeerObservation&) noexcept;
  void refreshDhtAccounting(bool ending = false) noexcept;
  void tickAccounting(bool force = false, bool ending = false) noexcept;
  void beginRecheck(const ui::RecheckRequest&, std::stop_token);
  void pollRecheck();
  void stopRecheckReceiver()noexcept;
  void closeWorkerRecheck();
  void deliverRecheck(ui::RecheckPresentation);
  void postRecheck(ui::RecheckPresentation);
  void publishOverall(bool closed = false);
  void publishHistoryChange() noexcept;
  void publishRuntimeMetrics(const engine::ApplicationPeerObservation&);
  void publishPeers();
  void publishTorrentDetail();
  ui::TorrentDetailInterest detailInterest_;
  std::optional<ui::TorrentDetailPresentation> latestTorrentDetail_;bool detailDeliveryQueued_=false;

  std::atomic_bool peersActive_{false};
  std::atomic_bool donationDialogOpen_{false};
  std::optional<ui::PeerPresentation> latestPeers_;bool peerDeliveryQueued_=false;
  void publishAzureus() noexcept;
  void publishLiveSpeed() noexcept;
  void stepClassicAutoSpeed();
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
  search::SavedSubscriptionService& ensureSubscriptions();
  void pollSubscriptions();
  void enqueueAutomaticSubscriptions(const std::vector<search::SavedSubscriptionResultSelector>&);
  void pollAutomaticSubscriptionQueue();
  void automaticSubscriptionNotice(QString);
  void pollSubscriptionAcquisition();
  void closeWorkerSubscriptionAcquisition();
  void finishWorkerSubscriptionAcquisition(QString failure = {});
  using SubscriptionTargetCheck=std::function<std::expected<void,storage::Error>(const core::TorrentHash&)>;
  static std::expected<bool,storage::Error> recoverSubscriptionAcquisitionBeforeHost(storage::ProfileLease&,const std::filesystem::path&);
  static std::expected<bool,storage::Error> recoverSubscriptionAcquisition(storage::ProfileLease&,const std::filesystem::path&,SubscriptionTargetCheck);
  static std::expected<bool,storage::Error> recoverTorrentIntakeBeforeHost(storage::ProfileLease&,const std::filesystem::path&);
  static std::expected<bool,storage::Error> recoverTorrentIntakeAfterSubscription(storage::ProfileLease&,const std::filesystem::path&,SubscriptionTargetCheck);
  void closeWorkerSubscriptions();
  void stopSubscriptionReceiver()noexcept;
  void subscriptionsDispatchInterrupted();
  void finishSubscriptionDispatch();
  void deliverSubscriptionCatalog(ui::SubscriptionCatalogPresentation);
  void deliverSubscription(ui::SubscriptionPresentation);
  void postSubscription(ui::SubscriptionPresentation);
  void deliverRssCatalog(ui::RssCatalogPresentation);
  void deliverRssFetch(ui::RssFetchPresentation);
  void postRssFetch(ui::RssFetchPresentation);
  void finishRssDispatch();
  void rssDispatchInterrupted();
  void stopRssReceiver()noexcept;
  void closeWorkerRss();
  void pollRssFetch();
  search::SavedMetasearchService& ensureMetasearch();
  void pollMetasearch();
  void pollMetasearchDownload();
  void stopMetasearchReceiver()noexcept;
  void closeWorkerMetasearch();
  void metasearchDispatchInterrupted();
  void finishMetasearchDispatch();
  void deliverMetasearchCatalog(ui::MetasearchCatalogPresentation);
  void deliverMetasearch(ui::MetasearchPresentation);
  void deliverMetasearchDownload(ui::MetasearchDownloadPresentation);
  void postMetasearch(ui::MetasearchPresentation);
  void postMetasearchDownload(ui::MetasearchDownloadPresentation);
  void finishPublicationDispatch();
  void publicationDispatchInterrupted();
  void stopPublicationReceiver()noexcept;
  void closeWorkerPublications();
  void pollPublications();
  void notePublicationUncertainty(ui::PublicationPacket&);
  void deliverPublication(ui::PublicationPresentation);
  void deliverPublicationCatalog(ui::PublicationCatalogPresentation);
  void postPublication(ui::PublicationPresentation);
  ui::PublicationCatalogPresentation readPublicationCatalog(ui::PublicationCatalogKey);
  bool closeSessionImpl(bool withoutCheckpoint,quint64 expectedSession);
  void postMediaRenderers(quint64 session, std::optional<ui::MediaRenderers>);
  void postMediaPlayback(quint64 session, ui::MediaPlaybackState, bool command);
  bool acceptLocalServiceResult(quint64 session, bool command);
  struct DownloadCommandResult {
    ui::Presentation presentation;
    QString message;
    bool failed;
  };
  DownloadCommandResult applyDownloadAction(const ui::DownloadTargets&, core::DownloadAction, std::stop_token);
  std::expected<void, storage::Error> controlLocalRpcDownloads(ui::DownloadTargets, core::DownloadAction);
  std::expected<void, storage::Error> recheckLocalRpcDownloads(ui::DownloadTargets);
  std::expected<void, storage::Error> removeLocalRpcDownload(ui::DownloadTargets);
  std::expected<void, storage::Error> setLocalRpcTorrentRates(ui::DownloadTargets, const std::array<std::optional<int>, 2>&);
  void applyTorrentOptions(const core::TorrentHash&, bool rates, std::array<int, 2> expected, std::array<int, 2> desired);
  std::shared_ptr<ui::FileOperationResultPacket> applyPreparedFileOperation(
      const ui::FileOperationSubmission&, std::stop_token);
  std::expected<qint64, storage::Error> allocateLocalRpcIdentity(
      const ui::Target&, const std::string&, storage::RpcIdentityMember);
  void openLocalRpc(quint16 port, plugins::rpc::HttpCredentials, std::string originalPluginDirectory,
      std::function<bool(const storage::ConfigFile&)> authorizeWrite = {});
  void finishLocalRpc(quint64 session, quint16 port);
  void refreshLocalServiceMonitor();
  void finishLocalMedia(quint64 session, quint16 httpPort, quint16 udpPort, QByteArray publishedUrl = {});
#ifndef _WIN32
  void initializeNativePluginUpdates();
  std::shared_ptr<const NativePluginUpdateChannel> nativePluginUpdates_;
#endif
  std::unique_ptr<Host> host_;
  std::optional<core::Ticket> current_;
  std::shared_ptr<std::atomic_bool> completionPosted_;
  QTimer* completionMonitor_ = nullptr;
  QTimer* idleMonitor_ = nullptr;
  QTimer* closeMonitor_=nullptr;
  std::chrono::steady_clock::time_point closeStarted_{};
  std::size_t closeStopped_=0,closeTotal_=0; // GUI-only, observational.

  std::shared_ptr<std::atomic_bool> mergeWake_=std::make_shared<std::atomic_bool>(false);
  QTimer* localServiceMonitor_ = nullptr;
  std::optional<core::Ticket> localServiceTicket_;
  bool mediaListening_ = false;
  bool rpcListening_ = false;
  QTimer* mergeMonitor_=nullptr;
  std::optional<core::Ticket> mergeTicket_;
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
  MetasearchFetchFactory metasearchFetchFactory_;
  MetasearchMetainfoFactory metasearchMetainfoFactory_;
  std::vector<std::pair<std::string,int>> mainlineBootstrap_; // Untrusted discovery hints; empty retains standard routers.
  std::optional<ui::MetasearchCatalogKey> guiMetasearchCatalog_;
  std::optional<ui::MetasearchQueryCommand> guiMetasearch_;
  std::optional<ui::MetasearchDownloadCommand> guiMetasearchDownload_;
  std::optional<ui::MetasearchQueryKey> startingMetasearch_;
  std::optional<ui::MetasearchDownloadKey> startingMetasearchDownload_;
  SubscriptionFetchFactory subscriptionFetchFactory_;
  SubscriptionAcquisitionFactory subscriptionAcquisitionFactory_;
  core::AtomicSharedPtr<std::stop_source> automaticSubscriptionCancellation_;
  std::atomic_bool automaticSubscriptionQueueCancelled_{false};
  SubscriptionFetchFactory automaticSubscriptionFetchFactory_;
  SubscriptionAcquisitionFactory automaticSubscriptionAcquisitionFactory_;
  std::optional<ui::SubscriptionCatalogKey> guiSubscriptionCatalog_;
  std::optional<ui::SubscriptionCommand> guiSubscription_;
  std::optional<ui::SubscriptionCommandKey> startingSubscription_;
  std::shared_ptr<std::atomic_bool> subscriptionCommitted_;
  std::optional<ui::RssCatalogRequest> guiRssCatalog_;
  std::optional<ui::RssFetchCommand> guiRssFetch_;
  std::optional<ui::RssFetchKey> rssStarting_;
  std::optional<ui::PublicationCatalogKey> guiPublicationCatalog_;
  std::optional<ui::PublicationCommand> guiPublication_;
  bool startingPublication_=false;
  std::optional<ui::RecheckRequest> guiRecheck_;
  quint64 guiRecheckSequence_=0;
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
