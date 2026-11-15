#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include "tag_strip_data.hpp"

namespace biglybtc::ui {

// Preview name filtering and completed/incomplete membership over LibraryModel.
class ClassicFilter final : public QSortFilterProxyModel {
public:
  explicit ClassicFilter(bool completed, QObject* parent = nullptr);
  void setQuery(QString query);
  void setTags(TagStripPresentation,QSet<QString>,bool inclusive);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
  const bool completed_;
  QString query_;TagStripPresentation tags_;TagStripSelection tagSelection_;QSet<QString> selectedTags_{AllTagChip};bool inclusive_=true;
};

} // namespace biglybtc::ui
