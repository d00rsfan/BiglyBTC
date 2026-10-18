#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace biglybtc::ui {

// Preview name filtering and completed/incomplete membership over LibraryModel.
class ClassicFilter final : public QSortFilterProxyModel {
public:
  explicit ClassicFilter(bool completed, QObject* parent = nullptr);
  void setQuery(QString query);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
  const bool completed_;
  QString query_;
};

} // namespace biglybtc::ui
