#pragma once
#include "runtime_metrics_presentation.hpp"
#include "biglybtc/disk_statistics.hpp"
#include <QPointer>
#include <QWidget>
#include <array>
class QLabel;class QTabWidget;
namespace biglybtc::ui {
class RuntimeMetricsView;
class CacheStatisticsView final:public QWidget {
  Q_OBJECT
public:
  explicit CacheStatisticsView(QWidget* parent=nullptr);
  bool beginSession(quint64);
  bool setPresentation(RuntimeMetricsPresentation);
  RuntimeMetricsPresentation presentation()const{return packet_;}
  core::DiskActivityFrame activity()const noexcept{return history_.snapshot();}
protected:
  void showEvent(QShowEvent*)override;bool eventFilter(QObject*,QEvent*)override;
private:
  void queueRefresh();void refresh();
  quint64 session_=0,revision_=0;
  RuntimeMetricsPresentation packet_;
  std::shared_ptr<const core::RuntimeMetrics> latestSnapshot_;
  core::DiskStatisticsReducer reducer_;
  core::DiskStatistics values_;
  core::DiskActivityHistory history_;
  QPointer<QLabel> status_;
  std::array<QPointer<QLabel>,8> facts_{};
  QPointer<QTabWidget> tabs_;
  QPointer<QWidget> summary_,plot_,observedWindow_;
  QPointer<RuntimeMetricsView> raw_;
  bool dirty_=true,rendering_=false,queued_=false;
};
} // namespace biglybtc::ui
