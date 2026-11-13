// SPDX-License-Identifier: GPL-2.0-only
#include "rating_delegate.hpp"
#include "rating_stars.hpp"
#include <QApplication>
#include <QPainter>
#include <algorithm>
namespace biglybtc::ui {
void RatingDelegate::paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const{
 const auto values=index.data(RatingGraphicRole).toMap();if(!values.contains(QStringLiteral("stars"))){QStyledItemDelegate::paint(painter,option,index);return;}
 QStyleOptionViewItem styled(option);initStyleOption(&styled,index);styled.text.clear();styled.icon=QIcon{};const auto* style=styled.widget?styled.widget->style():QApplication::style();style->drawControl(QStyle::CE_ItemViewItem,&styled,painter,styled.widget);
 if(!values.value(QStringLiteral("enabled"),true).toBool()){return;}const auto inside=option.rect.adjusted(1,0,-1,0);const auto image=ratingStarsForCell(values.value(QStringLiteral("stars")).toFloat(),values.value(QStringLiteral("personal")).toFloat(),option.palette.color(QPalette::Base).lightness()<128,inside.width());if(image.isNull()){return;}
 painter->save();painter->setClipRect(inside);painter->drawImage(QPoint(inside.x()+(inside.width()-image.width())/2,inside.y()+(inside.height()-image.height())/2),image);painter->restore();
}
QSize RatingDelegate::sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const{auto size=QStyledItemDelegate::sizeHint(option,index);size.setWidth(std::max(size.width(),RatingStarsWidth+4));size.setHeight(std::max(size.height(),RatingStarsHeight+2));return size;}
}
