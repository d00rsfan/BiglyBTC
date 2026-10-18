#pragma once

#include "engine_activity_presentation.hpp"
#include "engine_statistics_presentation.hpp"
#include "history_presentation.hpp"
#include "library_model.hpp"
#include "statistics_presentation.hpp"

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
class HistoryView;

// GUI-thread only; completions are accepted once for the current nonzero pair.
// This read-only page neither performs IO nor starts transfers/sampling timers.
// Supplied packets and snapshots must remain immutable for their shared lifetime.
class StatisticsPage final : public QWidget {
  Q_OBJECT
public:
  static constexpr std::size_t MaximumSamples = 600;
  explicit StatisticsPage(QWidget* parent = nullptr);
  void beginRequest(quint64 session, quint64 request);
  void setPresentation(StatisticsPresentation);
  void setFailed(quint64 session, quint64 request, const QString& message);
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
  void refreshRequested();
  void historyRequested(biglybtc::core::HistoryDate);
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
  QTabWidget* tabs_;
  QWidget* activity_;
  QTabWidget* activityViews_ = nullptr;
  EngineActivityView* engineActivity_ = nullptr;
  ActivityPlot* plot_;
  QLabel* transfer_;
  QLabel* downloadRate_;
  QLabel* uploadRate_;
  QLabel* observations_;
  QLabel* status_;
  QLabel* provenance_;
  QTabWidget* transferTabs_ = nullptr;
  HistoryView* history_ = nullptr;
  QWidget* engineDetails_ = nullptr;
  QLabel* engineStatus_ = nullptr;
  QLabel* engineTarget_ = nullptr;
  std::array<QLabel*, 8> engineValues_{};
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
};

} // namespace biglybtc::ui
