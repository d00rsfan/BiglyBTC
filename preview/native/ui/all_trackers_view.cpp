// SPDX-License-Identifier: GPL-2.0-or-later
#include "all_trackers_view.hpp"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace biglybtc::ui {
namespace {
constexpr std::size_t MaximumOrigins=2048,MaximumMembers=512;
}
AllTrackersView::AllTrackersView(QWidget* parent):QWidget(parent){
  auto* layout=new QVBoxLayout(this);
  auto* commands=new QHBoxLayout;
  refresh_=new QPushButton(tr("Refresh"),this);
  refresh_->setObjectName("all-trackers-refresh");
  commands->addWidget(refresh_);
  apply_=new QPushButton(tr("Show selected torrents"),this);
  apply_->setObjectName("all-trackers-apply");
  commands->addWidget(apply_);
  commands->addStretch();
  layout->addLayout(commands);
  status_=new QLabel(tr("Open a profile to view trackers"),this);
  status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true);
  layout->addWidget(status_);

  auto* columns=new QSplitter(Qt::Vertical,this);
  origins_=new QTreeWidget(columns);
  origins_->setObjectName("all-trackers-origins");
  origins_->setRootIsDecorated(false);
  origins_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  origins_->setHeaderLabels({tr("Tracker"),tr("Torrents")});
  origins_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
  origins_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
  torrents_=new QTreeWidget(columns);
  torrents_->setObjectName("all-trackers-members");
  torrents_->setRootIsDecorated(false);
  torrents_->setHeaderLabels({tr("Selected torrents")});
  layout->addWidget(columns,1);

  connect(refresh_,&QPushButton::clicked,this,[this]{emit loadRequested(false,{});});
  connect(apply_,&QPushButton::clicked,this,[this]{
    QStringList selected;
    for(int index=0;index<origins_->topLevelItemCount();++index){
      const auto* row=origins_->topLevelItem(index);
      if(row->isSelected()){selected.push_back(row->text(0));}
    }
    emit loadRequested(true,std::move(selected));
  });
  connect(origins_,&QTreeWidget::itemSelectionChanged,this,&AllTrackersView::updateControls);
  updateControls();
}
void AllTrackersView::begin(quint64 session){
  if(session_==session){return;}
  session_=session;
  request_=0;
  pending_=canSelect_=false;
  origins_->clear();
  torrents_->clear();
  status_->setText(session?tr("Refresh to load trackers"):tr("Open a profile to view trackers"));
  updateControls();
}
void AllTrackersView::requestSent(quint64 request){
  request_=request;
  pending_=true;
  status_->setText(tr("Reading trackers…"));
  updateControls();
}
void AllTrackersView::present(AllTrackersPresentation packet){
  if(!packet||!session_||packet->session!=session_||packet->request!=request_||
      !pending_||packet->origins.size()>MaximumOrigins||
      packet->members.size()>static_cast<qsizetype>(MaximumMembers)){return;}
  pending_=false;
  if(!packet->failure.isEmpty()){
    status_->setText(packet->failure);
    updateControls();
    return;
  }
  const QSignalBlocker selecting(origins_);
  origins_->clear();
  for(const auto& origin:packet->origins){
    auto* row=new QTreeWidgetItem(origins_,{origin.name,QString::number(origin.torrents)});
    row->setSelected(packet->selected.contains(origin.name));
  }
  torrents_->clear();
  for(const auto& torrent:packet->members){new QTreeWidgetItem(torrents_,{torrent});}
  canSelect_=packet->canSelect;
  status_->setText(packet->selectionActive
      ?tr("%n torrent(s) match the current tracker selection",nullptr,static_cast<int>(packet->members.size()))
      :tr("Select trackers to view their torrents"));
  updateControls();
}
void AllTrackersView::available(bool value){
  available_=value;
  if(value&&pending_){
    pending_=false;
    status_->setText(tr("The current tracker request did not complete"));
  }
  updateControls();
}
void AllTrackersView::updateControls(){
  const bool available=session_&&available_&&!pending_;
  refresh_->setEnabled(available);
  apply_->setEnabled(available&&canSelect_&&origins_->selectedItems().size()<=64);
}
}
