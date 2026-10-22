#pragma once
#include "manual_tag_presentation.hpp"

#include <QPointer>
#include <QWidget>

class QAbstractTableModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSortFilterProxyModel;
class QTableView;

namespace biglybtc::ui {
struct ManualTagTableData;
class ManualTagStatisticsView final : public QWidget {
  Q_OBJECT
public:
  explicit ManualTagStatisticsView(QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(ManualTagStatisticsPresentation);
  ManualTagStatisticsPresentation presentation() const { return packet_; }
signals:
  void readRequested(quint64 session);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void queueRefresh();
  void queueRead(bool explicitRead = false);
  void refresh();
  quint64 session_ = 0, revision_ = 0;
  bool dirty_ = true, rendering_ = false, queued_ = false;
  bool readSent_ = false, readQueued_ = false, explicitReadPending_ = false;
  ManualTagStatisticsPresentation packet_;
  std::shared_ptr<const ManualTagTableData> data_;
  QString search_;
  QPointer<QLabel> status_;
  QPointer<QLineEdit> filter_;
  QPointer<QPushButton> refresh_;
  quint64 renderedSession_ = 0, renderedEpoch_ = 0;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<QSortFilterProxyModel> proxy_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
