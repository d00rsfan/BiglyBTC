#pragma once

#include "biglybtc/executor.hpp"
#include "column_layout_presentation.hpp"
#include "library_model.hpp"
#include "saved_services_presentation.hpp"
#include "statistics_presentation.hpp"
#include "history_presentation.hpp"
#include "history_week_presentation.hpp"
#include "engine_statistics_presentation.hpp"

#include <QObject>

#include <filesystem>
#include <atomic>
#include <mutex>

class QTimer;

namespace biglybtc::app {

class SessionBridge final : public QObject {
  Q_OBJECT
public:
  explicit SessionBridge(QObject* parent = nullptr);
  ~SessionBridge() override;
  bool open(std::filesystem::path profile, unsigned short ownedPeerPort);
  bool refresh();
  bool savedServices(quint64 request);
  bool statistics(quint64 session, quint64 request);
  bool history(quint64 session, quint64 request, core::HistoryDate date);
  bool historyWeek(quint64 session, quint64 request, core::HistoryDate endingDate);
  bool columnLayouts(quint64 session, quint64 request);
  bool saveColumnLayout(ui::ColumnLayoutSaveSubmission);
  bool details(ui::Target target);
  bool continueDownload(ui::Target target);
  void cancel();
  bool busy() const { return busy_; }
signals:
  void snapshotReady(biglybtc::ui::Presentation);
  void progressReady(biglybtc::ui::Telemetry);
  void detailsReady(biglybtc::ui::Details);
  void savedServicesReady(biglybtc::ui::SavedServicesPresentation);
  void statisticsReady(biglybtc::ui::StatisticsPresentation);
  void historyReady(biglybtc::ui::HistoryPresentation);
  void historyWeekReady(biglybtc::ui::HistoryWeekPresentation);
  void engineStatisticsReady(biglybtc::ui::EngineStatisticsPresentation);
  void columnLayoutSessionReady(quint64 session);
  void columnLayoutsReady(biglybtc::ui::ColumnLayoutsPresentation);
  void columnLayoutSaved(biglybtc::ui::ColumnLayoutSavePresentation);
  void busyChanged(bool);
  void messageReady(QString);
private:
  struct Host;
  bool dispatch(std::move_only_function<void(std::stop_token)> operation, bool recoverLibrarySnapshot = true);
  void ready(ui::Presentation snapshot, QString message = {});
  void failed(QString message);
  void progress(ui::Telemetry update);
  void engineStatistics(ui::EngineStatisticsPresentation);
  std::unique_ptr<Host> host_;
  std::optional<core::Ticket> current_;
  std::shared_ptr<std::atomic_bool> completionPosted_;
  QTimer* completionMonitor_ = nullptr;
  std::mutex progressMutex_;
  std::optional<ui::Telemetry> latestProgress_;
  bool deliveryQueued_ = false;
  std::optional<ui::EngineStatisticsPresentation> latestEngineStatistics_;
  bool engineDeliveryQueued_ = false;
  bool busy_ = false;
  // GUI-thread publication only; worker commands capture the requested identity.
  quint64 publishedColumnLayoutSession_ = 0;
  // Destroy/join this worker before host/lease, tickets, mutex or QObject context.
  core::Executor worker_{8};
};
} // namespace biglybtc::app
