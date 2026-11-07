// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHeaderView>
#include <QStyleOptionHeader>
namespace biglybtc::ui {
class ClassicSortHeader : public QHeaderView {
public:
  explicit ClassicSortHeader(QWidget* parent=nullptr):QHeaderView(Qt::Horizontal,parent){setSectionsClickable(true);setHighlightSections(true);}
protected:
  void initStyleOptionForIndex(QStyleOptionHeader* option,int logicalIndex)const override{
    QHeaderView::initStyleOptionForIndex(option,logicalIndex);
    if(option->sortIndicator!=QStyleOptionHeader::None){
      option->sortIndicator=sortIndicatorOrder()==Qt::AscendingOrder?QStyleOptionHeader::SortUp:QStyleOptionHeader::SortDown;
    }
  }
};
}
