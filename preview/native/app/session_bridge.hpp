#pragma once

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
#include "country_statistics_presentation.hpp"

#include <QObject>

#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>

class QTimer;

namespace biglybtc::engine { struct ApplicationPeerObservation; }
namespace biglybtc::storage { class ProfileLease; class CountryProvider; }

namespace biglybtc::app {

class OwnedAzureusService;

class SessionBridge final : public QObject {
  Q_OBJECT
public:
  using CountryProviderFactory = std::function<std::unique_ptr<storage::CountryProvider>(const storage::ProfileLease&)>;
  using AzureusServiceFactory = std::function<std::unique_ptr<OwnedAzureusService>(const storage::ProfileLease&)>;
  explicit SessionBridge(QObject* parent = nullptr, CountryProviderFactory = {}, AzureusServiceFactory = {});
  ~SessionBridge() override;
  bool open(std::filesystem::path profile, unsigned short ownedPeerPort);
  bool refresh();
  bool savedServices(quint64 request);
  bool statistics(quint64 session, quint64 request);
  bool markStatistics(quint64 session, bool clear);
  bool manualTagStatistics(quint64 session);
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
  bool busy() const { return busy_; }
signals:
  void snapshotReady(biglybtc::ui::Presentation);
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
  void manualTagStatisticsReady(biglybtc::ui::ManualTagStatisticsPresentation);
  void countryStatisticsReady(biglybtc::ui::CountryStatisticsPresentation);
  void sessionClosed(quint64 session, bool joined, bool storageReleased);
  void sessionCloseFailed();
  void busyChanged(bool);
  void messageReady(QString);
private:
  struct Host;
  bool dispatch(std::move_only_function<void(std::stop_token)> operation, bool recoverLibrarySnapshot = true, bool closingOperation = false);
  void ready(ui::Presentation snapshot, QString message = {});
  void failed(QString message);
  void progress(ui::Telemetry update);
  void engineStatistics(ui::EngineStatisticsPresentation);
  void nativeSession(ui::NativeSessionPresentation);
  void initializePersistentOwner(std::stop_token);
  void observeNative(const engine::ApplicationPeerObservation&) noexcept;
  void tickAccounting(bool force = false, bool ending = false) noexcept;
  void publishOverall(bool closed = false);
  void publishHistoryChange() noexcept;
  void publishRuntimeMetrics(const engine::ApplicationPeerObservation&);
  void publishAzureus() noexcept;
  ui::ManualTagStatisticsPresentation manualTagPacket();
  void publishManualTags() noexcept;
  void deliverManualTags(ui::ManualTagStatisticsPresentation);
  ui::CountryStatisticsPresentation countryPacket();
  void publishCountries() noexcept;
  void deliverCountries(ui::CountryStatisticsPresentation);
  void closeFailed(QString);
  std::unique_ptr<Host> host_;
  std::optional<core::Ticket> current_;
  std::shared_ptr<std::atomic_bool> completionPosted_;
  QTimer* completionMonitor_ = nullptr;
  QTimer* idleMonitor_ = nullptr;
  std::optional<core::Ticket> idlePoll_;
  bool closingSession_ = false;
  bool profileClosed_ = false; // GUI-only: closed admission differs from never-opened failure routing.
  std::mutex progressMutex_;
  std::optional<ui::Telemetry> latestProgress_;
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
  std::optional<ui::CountryStatisticsPresentation> latestCountries_;
  bool countryDeliveryQueued_ = false;
  ui::CountryStatisticsPresentation deliveredCountries_; // GUI-only admission state.
  CountryProviderFactory countryProviderFactory_; // Invoked only by the worker.
  AzureusServiceFactory azureusServiceFactory_; // Explicit owner policy; absent means disabled.
  std::optional<ui::AzureusStatisticsPresentation> latestAzureus_;
  bool azureusDeliveryQueued_ = false;
  bool busy_ = false;
  // GUI-thread publication only; worker commands capture the requested identity.
  quint64 publishedColumnLayoutSession_ = 0;
  // Destroy/join this worker before host/lease, tickets, mutex or QObject context.
  core::Executor worker_{8};
};
} // namespace biglybtc::app
