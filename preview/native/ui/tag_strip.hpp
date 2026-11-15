// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_strip_data.hpp"
#include "tag_strip_commands.hpp"
#include <QWidget>
#include <QMetaType>
class QScrollArea;class QMenu;class QTableView;
namespace biglybtc::ui {
void installTagDragSource(QTableView*);
class TagStrip final:public QWidget {
 Q_OBJECT
public:
 explicit TagStrip(bool completed,QWidget* parent=nullptr);
 void setSnapshot(TagStripPresentation,TagStripSettings,QString failure={});
 void beginSession();void setWritable(bool value){writable_=value;}
 const TagStripSelection& selection()const noexcept{return selection_;}
 void contextMenu(QPoint global,QString chip={});
 QSize sizeHint()const override;QSize minimumSizeHint()const override;
signals:
 void assignmentRequested(QString chip);
 void assignmentDropped(QString chip,QObject* source,QByteArray hashes);
 void selectionChanged();
 void settingsRequested(biglybtc::ui::TagStripSettings expected,biglybtc::ui::TagStripSettings value);
 void categoriesRequested();void tagsRequested();
protected:
 void resizeEvent(QResizeEvent*)override;
private:
 void rebuild();void updateButtons();
 quint64 binding_=1;bool completed_,writable_=false;TagStripPresentation data_;TagStripSettings settings_;TagStripSelection selection_;QString failure_;
 QScrollArea* scroll_;QWidget* content_;QVector<QString> visibleKeys_;int desiredHeight_=26;
};
}
