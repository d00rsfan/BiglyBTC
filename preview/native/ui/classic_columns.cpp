#include "classic_columns.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QTableView>

namespace biglybtc::ui {

// Original reference: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TableColumnCreator.createIncompleteDM/createCompleteDM and
// TableViewSWT_Common.fillColumnMenu. Only existing preview columns are offered.
ClassicColumns::ClassicColumns(QTableView& table, bool completed)
    : QObject(&table), table_(&table),
      menu_(new QMenu(completed ? tr("Complete Columns") : tr("Incomplete Columns"), &table)) {
  const auto prefix = completed ? QStringLiteral("classic-columns-complete")
                                : QStringLiteral("classic-columns-incomplete");
  setObjectName(prefix + QStringLiteral("-controller"));
  menu_->setObjectName(prefix + QStringLiteral("-menu"));
  auto* header = table.horizontalHeader();
  const int count = header->count();
  columns_.reserve(static_cast<std::size_t>(count));
  order_.reserve(static_cast<std::size_t>(count));
  actions_.reserve(static_cast<std::size_t>(count));
  for (int visual = 0; visual < count; ++visual) { order_.push_back(header->logicalIndex(visual)); }
  for (int logical = 0; logical < count; ++logical) {
    bool hidden = table.isColumnHidden(logical);
    // Hidden sections report zero width; reveal only while taking this initial
    // snapshot so Reset retains their stored width when they are later shown.
    if (hidden) { header->showSection(logical); }
    const int width = header->sectionSize(logical);
    if (hidden) { header->hideSection(logical); }
    if (logical == 4) { hidden = completed; }
    if (logical == 5) { hidden = false; }
    if (logical == 8) { hidden = true; }
    columns_.push_back({logical, width, hidden});
    auto label = table.model()->headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString();
    if (label.isEmpty()) { label = tr("Column %1").arg(logical + 1); }
    auto* action = menu_->addAction(label.replace(QStringLiteral("&"), QStringLiteral("&&")));
    action->setObjectName(prefix + QStringLiteral("-column-%1").arg(logical));
    action->setCheckable(true);
    actions_.push_back(action);
    connect(action, &QAction::toggled, this, [this, logical](bool visible) {
      if (!table_ || logical >= table_->horizontalHeader()->count()) { return; }
      table_->setColumnHidden(logical, !visible);
      refreshActions();
    });
  }
  menu_->addSeparator();
  auto* restore = menu_->addAction(tr("Reset Columns"));
  restore->setObjectName(prefix + QStringLiteral("-reset"));
  connect(restore, &QAction::triggered, this, &ClassicColumns::reset);
  connect(menu_, &QMenu::aboutToShow, this, &ClassicColumns::refreshActions);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(header, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
    if (table_ && menu_) { menu_->popup(table_->horizontalHeader()->viewport()->mapToGlobal(point)); }
  });
  reset();
}

ClassicColumns::~ClassicColumns() { delete menu_.data(); }
QMenu* ClassicColumns::menu() const { return menu_.data(); }

void ClassicColumns::refreshActions() {
  if (!table_ || !menu_) { return; }
  const int count = table_->horizontalHeader()->count();
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    auto* action = actions_[i].data();
    if (!action) { continue; }
    const QSignalBlocker guard(action);
    const bool available = columns_[i].logical < count;
    action->setEnabled(available);
    action->setChecked(available && !table_->isColumnHidden(columns_[i].logical));
  }
}

void ClassicColumns::reset() {
  if (!table_) { return; }
  auto* header = table_->horizontalHeader();
  for (std::size_t visual = 0; visual < order_.size(); ++visual) {
    const int current = header->visualIndex(order_[visual]);
    if (current >= 0 && static_cast<int>(visual) < header->count()) {
      header->moveSection(current, static_cast<int>(visual));
    }
  }
  for (const auto& column : columns_) {
    if (column.logical >= header->count()) { continue; }
    table_->setColumnHidden(column.logical, column.hidden);
    header->resizeSection(column.logical, column.width);
  }
  refreshActions();
}

} // namespace biglybtc::ui
