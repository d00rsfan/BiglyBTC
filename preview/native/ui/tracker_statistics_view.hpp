#pragma once
#include "tracker_statistics_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "tracker_activity_history.hpp"
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
  bool setRuntimeMetrics(RuntimeMetricsPresentation);
  TrackerActivityFrame activity()const noexcept{return history_.frame();}
  TrackerStatisticsPresentation presentation()const{return packet_;}
protected:
  void showEvent(QShowEvent*)override;
  bool eventFilter(QObject*,QEvent*)override;
private:
  void queueRefresh();void refresh();
  quint64 session_=0,revision_=0;
  bool dirty_=true,rendering_=false,queued_=false;
  TrackerStatisticsPresentation packet_;
  RuntimeMetricsPresentation runtimePacket_;
  std::shared_ptr<const core::RuntimeMetrics> latestRuntime_;
  TrackerActivityHistory history_;
  QString search_;
  QPointer<QLabel> status_,queueFact_,trafficFact_,historyStatus_;
  QPointer<QWidget> historyPlot_;
  QPointer<QLineEdit> filter_;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<QSortFilterProxyModel> proxy_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
