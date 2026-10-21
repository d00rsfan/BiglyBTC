#pragma once

#include "history_range_presentation.hpp"
#include "saved_history_traffic.hpp"

#include <QPointer>
#include <QWidget>

#include <array>
#include <cstdint>

class QComboBox;
class QLabel;
class QPushButton;
class QShowEvent;

namespace biglybtc::ui {
class HistoryRangePlot;

// Immutable result browser. Only the GUI request boundary reads the UTC clock;
// the saved reader receives that explicit timestamp and owns no hidden clock.
class HistoryRangeView final : public QWidget {
  Q_OBJECT
public:
  explicit HistoryRangeView(QWidget* parent = nullptr,
      core::HistoryPeriod initialPeriod = core::HistoryPeriod::Week,
      std::int64_t initialOffset = 0,
      SavedHistoryTraffic initialTraffic = SavedHistoryTraffic::Payload);
  void beginRequest(quint64 session, quint64 request, core::HistoryRangeRequest);
  void setPresentation(HistoryRangePresentation);
  void setFailed(quint64 session, quint64 request, core::HistoryRangeRequest, const QString&);
  void resetSession(quint64 session);
  core::HistoryRangeRequest query() const { return query_; }
  core::HistoryPeriod period() const { return query_.period; }
  std::int64_t offset() const { return query_.offset; }
  SavedHistoryTraffic traffic() const;
  HistoryRangePresentation presentation() const { return frame_; }
public slots:
  void requestRefresh();
signals:
  void readRequested(biglybtc::core::HistoryRangeRequest);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  bool advance();
  bool accepts(quint64, quint64, core::HistoryRangeRequest) const;
  void choiceChanged();
  void movePeriod(int);
  void queueTurn();
  void requestRead();
  void render();
  void observeWindow();
  core::HistoryRangeRequest query_;
  HistoryRangePresentation frame_;
  quint64 session_ = 0, request_ = 0, revision_ = 0;
  bool loading_ = false, dirty_ = true, needsRead_ = true;
  bool turnQueued_ = false, exhausted_ = false;
  QComboBox* period_ = nullptr;
  QComboBox* traffic_ = nullptr;
  QPushButton* previous_ = nullptr;
  QPushButton* next_ = nullptr;
  QLabel* range_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* resolution_ = nullptr;
  QLabel* coverage_ = nullptr;
  std::array<QLabel*, 6> totals_{};
  HistoryRangePlot* plot_ = nullptr;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
