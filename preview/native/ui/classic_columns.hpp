#pragma once

#include <QObject>
#include <QPointer>

#include <vector>

class QAction;
class QMenu;
class QTableView;

namespace biglybtc::ui {

// One table-owned controller for the table's construction-time columns.
// Visibility and reset state are independent of the source model and profile.
class ClassicColumns final : public QObject {
public:
  explicit ClassicColumns(QTableView& table, bool completed);
  ~ClassicColumns() override;
  QMenu* menu() const;
  void reset();

private:
  struct Column {
    int logical;
    int width;
    bool hidden;
  };
  void refreshActions();
  QPointer<QTableView> table_;
  QPointer<QMenu> menu_;
  std::vector<Column> columns_;
  std::vector<int> order_;
  std::vector<QPointer<QAction>> actions_;
};

} // namespace biglybtc::ui
