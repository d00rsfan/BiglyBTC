// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QPointer>
#include <QScrollBar>
#include <QTableView>
#include <array>

namespace biglybtc::ui {
// Restore selection/data atomically without revealing an offscreen current row.
// Suppress automatic current-index scrolling before any model notifications;
// keep painting suspended until the original viewport has been restored.
class TableViewportGuard final {
public:
  explicit TableViewportGuard(QTableView* table) : TableViewportGuard(table, nullptr) {}
  TableViewportGuard(QTableView* first, QTableView* second)
      : saved_{{save(first), save(second)}} {
    for (const auto& saved : saved_) {
      if (saved.table) {
        saved.table->setAutoScroll(false);
        saved.table->setUpdatesEnabled(false);
      }
    }
  }
  ~TableViewportGuard() {
    for (const auto& saved : saved_) {
      if (saved.table) {
        saved.table->horizontalScrollBar()->setValue(saved.horizontal);
        if(!saved.table){continue;}
        saved.table->verticalScrollBar()->setValue(saved.vertical);
        if(!saved.table){continue;}
        saved.table->setAutoScroll(saved.autoScroll);
        if(!saved.table){continue;}
        saved.table->setUpdatesEnabled(saved.updatesEnabled);
      }
    }
  }
  TableViewportGuard(const TableViewportGuard&) = delete;
  TableViewportGuard& operator=(const TableViewportGuard&) = delete;
private:
  struct Saved {
    QPointer<QTableView> table;
    int horizontal = 0, vertical = 0;
    bool autoScroll = false, updatesEnabled = false;
  };
  static Saved save(QTableView* table) {
    return table ? Saved{table, table->horizontalScrollBar()->value(),
        table->verticalScrollBar()->value(), table->hasAutoScroll(), table->updatesEnabled()}
        : Saved{};
  }
  std::array<Saved, 2> saved_;
};
}
