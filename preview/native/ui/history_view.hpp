#pragma once

#include "history_presentation.hpp"

#include <QPointer>
#include <QWidget>

#include <optional>

class QDateEdit;
class QLabel;
class QPushButton;
class QShowEvent;
class QTableView;

namespace biglybtc::ui {

// GUI-thread only. No IO, sampling timer, writer, or filesystem path exposure.
// Hidden/minimized views cache immutable deliveries and render on activation.
class HistoryView final : public QWidget {
  Q_OBJECT
public:
  explicit HistoryView(QWidget* parent = nullptr);
  void beginRequest(quint64 session, quint64 request, core::HistoryDate);
  void setPresentation(HistoryPresentation);
  void setFailed(quint64 session, quint64 request, core::HistoryDate, const QString&);
  void resetSession(quint64 session);
  core::HistoryDate selectedDate() const;
  HistoryPresentation presentation() const { return frame_; }
signals:
  void readRequested(biglybtc::core::HistoryDate);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class HoursModel;
  bool accepts(quint64 session, quint64 request, core::HistoryDate) const;
  bool advance();
  void refresh();
  void requestRead();
  void dateChanged();
  void observeWindow();
  QDateEdit* date_ = nullptr;
  QPushButton* previous_ = nullptr;
  QPushButton* next_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* downloaded_ = nullptr;
  QLabel* uploaded_ = nullptr;
  QLabel* observations_ = nullptr;
  QTableView* table_ = nullptr;
  HoursModel* model_ = nullptr;
  QPointer<QWidget> observedWindow_;
  HistoryPresentation frame_;
  std::optional<core::HistoryDate> requestedDate_;
  quint64 session_ = 0, request_ = 0, revision_ = 0;
  bool loading_ = false, dirty_ = true, exhausted_ = false;
};

} // namespace biglybtc::ui
