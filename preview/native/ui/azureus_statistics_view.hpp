#pragma once
#include "azureus_statistics_presentation.hpp"
#include <QPointer>
#include <QWidget>
class QLabel;
class QTableView;
class QAbstractTableModel;
class QShowEvent;
namespace biglybtc::ui {
enum class AzureusStatisticsSection { Dht, Operations, Vivaldi };
class AzureusStatisticsView final:public QWidget {
  Q_OBJECT
public:
  explicit AzureusStatisticsView(AzureusStatisticsSection,QWidget* parent=nullptr);
  bool beginSession(quint64);
  bool setPresentation(AzureusStatisticsPresentation);
  AzureusStatisticsPresentation presentation()const{return packet_;}
protected:
  void showEvent(QShowEvent*)override;
  bool eventFilter(QObject*,QEvent*)override;
private:
  class PositionPlot;
  class OperationTimeline;
  void queueRefresh();void refresh();
  const AzureusStatisticsSection section_;
  quint64 session_=0,revision_=0;bool dirty_=true,queued_=false,rendering_=false;
  AzureusStatisticsPresentation packet_;
  QPointer<QLabel> state_,coordinates_;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<PositionPlot> plot_;
  QPointer<OperationTimeline> timeline_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
