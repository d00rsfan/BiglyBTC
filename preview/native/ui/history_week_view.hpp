#pragma once

#include "history_week_presentation.hpp"
#include "saved_history_traffic.hpp"

#include <QPointer>
#include <QWidget>

#include <optional>

class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QShowEvent;
class QTableView;

namespace biglybtc::ui {

class SavedHistoryPlot;

// GUI-thread-only saved records browser. No IO, timer, totals across days,
// directory scan or live history service. Supplied frames remain immutable.
class HistoryWeekView final : public QWidget {
  Q_OBJECT
public:
  explicit HistoryWeekView(QWidget* parent = nullptr,
      std::optional<core::HistoryDate> endingDate = {}, bool hourly = false,
      SavedHistoryTraffic initialTraffic = SavedHistoryTraffic::Payload);
  void beginRequest(quint64 session, quint64 request, core::HistoryDate endingDate);
  void setPresentation(HistoryWeekPresentation);
  void setFailed(quint64 session, quint64 request, core::HistoryDate endingDate, const QString&);
  void resetSession(quint64 session);
  core::HistoryDate selectedDate() const;
  bool hourlyResolution() const;
  SavedHistoryTraffic traffic() const;
  HistoryWeekPresentation presentation() const { return frame_; }
signals:
  void readRequested(biglybtc::core::HistoryDate);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class DaysModel;
  bool accepts(quint64 session, quint64 request, core::HistoryDate) const;
  bool advance();
  void refresh();
  void requestRead();
  void dateChanged();
  void observeWindow();
  QDateEdit* date_ = nullptr;
  QComboBox* resolution_ = nullptr;
  QComboBox* traffic_ = nullptr;
  QPushButton* previous_ = nullptr;
  QPushButton* next_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* range_ = nullptr;
  SavedHistoryPlot* plot_ = nullptr;
  QTableView* table_ = nullptr;
  DaysModel* model_ = nullptr;
  QPointer<QWidget> observedWindow_;
  HistoryWeekPresentation frame_;
  std::optional<core::HistoryDate> requestedDate_;
  quint64 session_ = 0, request_ = 0, revision_ = 0;
  bool loading_ = false, dirty_ = true, exhausted_ = false;
};

} // namespace biglybtc::ui
