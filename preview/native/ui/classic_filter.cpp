#include "classic_filter.hpp"

#include "library_model.hpp"

#include <utility>

namespace biglybtc::ui {

ClassicFilter::ClassicFilter(bool completed, QObject* parent)
    : QSortFilterProxyModel(parent), completed_(completed) {
  setDynamicSortFilter(true);
  setSortRole(Qt::UserRole);
  setFilterRole(LibraryModel::CompletionRole);
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  // Completion changes arrive on the progress columns, name changes on column 0.
  // Every changed column must therefore be relevant to this row predicate.
  setFilterKeyColumn(-1);
}

void ClassicFilter::setQuery(QString query) {
  if (query_ == query) { return; }
  beginFilterChange();
  query_ = std::move(query);
  endFilterChange(Direction::Rows);
}

bool ClassicFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  const auto* model = sourceModel();
  if (!model || sourceParent.isValid()) { return false; }
  const auto index = model->index(sourceRow, 0, sourceParent);
  if (!index.isValid()) { return false; }
  const auto completion = model->data(index, LibraryModel::CompletionRole);
  return completion.isValid() && completion.toBool() == completed_
      && model->data(index, Qt::DisplayRole).toString().contains(query_, Qt::CaseInsensitive);
}

} // namespace biglybtc::ui
