#pragma once

#include "runtime_metrics_presentation.hpp"

#include <QPointer>
#include <QWidget>
#include <optional>
#include <array>

class QAbstractTableModel;
class QLabel;
class QLineEdit;
class QShowEvent;
class QSortFilterProxyModel;
class QTableView;

namespace biglybtc::ui {

// GUI-only bounded view; snapshots supplied here must remain immutable. No IO,
// sampling, service initialization, history, timers or automatic refresh reads.
class RuntimeMetricsView final : public QWidget {
  Q_OBJECT
public:
  explicit RuntimeMetricsView(std::optional<core::RuntimeMetricGroup> filter = {},
      QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(RuntimeMetricsPresentation);
  RuntimeMetricsPresentation presentation() const { return packet_; }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void refresh();
  void queueRefresh();
  quint64 session_ = 0, revision_ = 0, snapshotSequence_ = 0;
  qint64 snapshotTime_ = 0;
  quint64 renderedSession_ = 0, renderedEpoch_ = 0;
  RuntimeMetricsPresentation packet_;
  QString search_;
  bool dirty_ = true, rendering_ = false, queued_ = false;
  QPointer<QLabel> status_;
  std::optional<core::RuntimeMetricGroup> summaryGroup_;
  std::array<QPointer<QLabel>, 3> summaries_{};
  std::size_t summaryCount_ = 0;
  QPointer<QLineEdit> filter_;
  QPointer<QTableView> table_;
  QPointer<QAbstractTableModel> model_;
  QPointer<QSortFilterProxyModel> proxy_;
  QPointer<QWidget> observedWindow_;
};

} // namespace biglybtc::ui
