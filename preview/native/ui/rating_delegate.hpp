// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QStyledItemDelegate>
namespace biglybtc::ui {
class RatingDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
 void paint(QPainter*,const QStyleOptionViewItem&,const QModelIndex&)const override;
 QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&)const override;
};
}
