#pragma once
#include "country_statistics_presentation.hpp"
#include <QPointer>
#include <QWidget>

class QAbstractTableModel;
class QLabel;
class QLineEdit;
class QShowEvent;
class QSortFilterProxyModel;
class QTableView;
namespace biglybtc::ui {
class CountryStatisticsView final : public QWidget {
  Q_OBJECT
public:
  explicit CountryStatisticsView(QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(CountryStatisticsPresentation);
  CountryStatisticsPresentation presentation() const { return packet_; }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void queueRefresh();
  void refresh();
  quint64 session_ = 0, revision_ = 0;
  bool dirty_ = true, rendering_ = false, queued_ = false;
  CountryStatisticsPresentation packet_;
  QString search_;
  QPointer<QLabel> status_;
  QPointer<QLineEdit> filter_;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<QSortFilterProxyModel> proxy_;
  QPointer<QWidget> observedWindow_;
  quint64 renderedSession_ = 0, renderedEpoch_ = 0;
};
} // namespace biglybtc::ui
