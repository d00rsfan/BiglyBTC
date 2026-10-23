#pragma once
#include "tracker_statistics_presentation.hpp"
#include <QPointer>
#include <QWidget>
class QLabel;class QLineEdit;class QTableView;class QAbstractTableModel;class QSortFilterProxyModel;
namespace biglybtc::ui {
class TrackerStatisticsView final:public QWidget {
  Q_OBJECT
public:
  explicit TrackerStatisticsView(QWidget* parent=nullptr);
  bool beginSession(quint64);
  bool setPresentation(TrackerStatisticsPresentation);
  TrackerStatisticsPresentation presentation()const{return packet_;}
protected:
  void showEvent(QShowEvent*)override;
  bool eventFilter(QObject*,QEvent*)override;
private:
  void queueRefresh();void refresh();
  quint64 session_=0,revision_=0;
  bool dirty_=true,rendering_=false,queued_=false;
  TrackerStatisticsPresentation packet_;
  QString search_;
  QPointer<QLabel> status_;
  QPointer<QLineEdit> filter_;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<QSortFilterProxyModel> proxy_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
