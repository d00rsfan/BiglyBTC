// SPDX-License-Identifier: GPL-2.0-or-later
#include "activity_centre.hpp"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextDocument>
#include <QTimer>
namespace biglybtc::ui {
namespace {class TextOnly final:public QTextDocument {QVariant loadResource(int,const QUrl&)override{return {};}};}
ActivityCentre::ActivityCentre(QWidget* parent):QWidget(parent){setObjectName("activity-centre");auto* layout=new QVBoxLayout(this);status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);auto* row=new QHBoxLayout;
 for(const auto& pair:{std::pair{"Refresh","refresh"},{"Mark read","read"},{"Mark unread","unread"},{"Remove","remove"}}){auto* button=new QPushButton(tr(pair.first),this);button->setObjectName(QStringLiteral("activity-")+pair.second);row->addWidget(button);connect(button,&QPushButton::clicked,this,[this,action=QString::fromLatin1(pair.second)]{change(action);});}
 auto* open=new QPushButton(tr("View"),this);open->setObjectName("activity-view");row->addWidget(open);connect(open,&QPushButton::clicked,this,&ActivityCentre::view);auto* settings=new QPushButton(tr("Notification settings…"),this);row->addWidget(settings);connect(settings,&QPushButton::clicked,this,[this]{if(!value_.session){return;}const auto session=value_.session;const auto expected=value_.settings;auto* dialog=new ChatNotificationDialog(expected,this);connect(dialog,&QDialog::accepted,this,[this,session,expected,dialog]{if(value_.session==session){emit settingsRequested(session,expected,dialog->settings());}});connect(dialog,&ChatNotificationDialog::testSoundRequested,this,&ActivityCentre::soundRequested);dialog->open();});layout->addLayout(row);
 table_=new QTableWidget(0,3,this);table_->setObjectName("activity-table");table_->setHorizontalHeaderLabels({tr("Date"),tr("Read"),tr("Notification")});table_->horizontalHeader()->setStretchLastSection(true);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::ExtendedSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table_,1);connect(table_,&QTableWidget::doubleClicked,this,[this]{view();});auto* refresh=new QTimer(this);refresh->setInterval(5000);connect(refresh,&QTimer::timeout,this,[this]{if(isVisible()){change(QStringLiteral("refresh"));}});refresh->start();}
QVector<ActivityRefView> ActivityCentre::selected()const{QVector<ActivityRefView> out;for(const auto& row:table_->selectionModel()->selectedRows()){if(row.row()>=0&&row.row()<value_.rows.size()){out.push_back(value_.rows[row.row()].source);}}return out;}
void ActivityCentre::change(QString action){if(value_.session&&(action=="refresh"||value_.writable)){emit command(value_.session,std::move(action),selected());}}
void ActivityCentre::view(){const auto refs=selected();if(refs.size()!=1){return;}for(const auto& row:value_.rows){if(row.source==refs[0]&&row.canView){emit viewRequested(value_.session,row.key,row.source);return;}}}
void ActivityCentre::setSnapshot(ActivityView value){const auto refs=selected();value_=std::move(value);status_->setText(value_.status);table_->setRowCount(0);for(const auto& row:value_.rows){const int index=table_->rowCount();table_->insertRow(index);table_->setItem(index,0,new QTableWidgetItem(row.time.toLocalTime().toString(Qt::ISODate)));table_->setItem(index,1,new QTableWidgetItem(row.read?tr("Yes"):tr("No")));TextOnly doc;doc.setHtml(row.text);table_->setItem(index,2,new QTableWidgetItem(doc.toPlainText()));if(refs.contains(row.source)){table_->selectionModel()->select(table_->model()->index(index,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);}}}
}
