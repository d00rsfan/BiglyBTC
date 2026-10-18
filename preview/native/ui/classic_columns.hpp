#pragma once

#include "biglybtc/column_layout.hpp"

#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QTableView;

namespace biglybtc::ui {

// Table-owned local choices and construction-time reset baseline. Saved layout
// import is read-only, layout-only and never replaces that reset baseline.
class ClassicColumns final : public QObject {
public:
  explicit ClassicColumns(QTableView& table, bool completed);
  ~ClassicColumns() override;
  QMenu* menu() const;
  void reset();
  // Null means the monotonic edit counter is exhausted; imports then fail shut.
  std::optional<quint64> editRevision() const noexcept;
  // Optional predicate is borrowed only during this call, never retained.
  bool applyLayout(const core::SavedTableLayout&, quint64 expectedRevision,
      const std::function<bool()>& stillCurrent = {});

private:
  struct InternalChange;
  struct Column {
    int logical;
    int width;
    bool hidden;
  };
  void refreshActions();
  void markEdit(bool explicitIntent = false);
  void restoreBaseline(bool localIntent);
  QPointer<QTableView> table_;
  QPointer<QMenu> menu_;
  std::vector<Column> columns_;
  std::vector<int> order_;
  std::vector<QPointer<QAction>> actions_;
  quint64 editRevision_ = 0;
  bool editRevisionExhausted_ = false;
  bool internalChange_ = false;
};

} // namespace biglybtc::ui
