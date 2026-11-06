// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_history_page.hpp"
#include <QDateTime>
#include <QByteArray>
#include <QStringList>
#include <QVariant>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
namespace biglybtc::ui {
namespace {
QString text(const std::string& value){return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));}
QString date(std::int64_t value){if(value<=0){return QStringLiteral("—");}const auto time=QDateTime::fromMSecsSinceEpoch(value);return time.isValid()?time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")):QStringLiteral("—");}
QString hashText(const std::optional<std::string>& value){return value?QString::fromLatin1(QByteArray(value->data(),static_cast<qsizetype>(value->size())).toHex().toUpper()):QString{};}
}
DownloadHistoryPage::DownloadHistoryPage(QWidget* parent):QWidget(parent),status_(new QLabel(this)),table_(new QTableWidget(this)),reload_(new QPushButton(tr("Reload"),this)),erase_(new QPushButton(tr("Remove selected record…"),this)),clear_(new QPushButton(tr("Clear saved history…"),this)),reset_(new QPushButton(tr("Reset from stopped library…"),this)){
  setObjectName(QStringLiteral("download-history-page"));status_->setObjectName(QStringLiteral("download-history-status"));status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);
  table_->setObjectName(QStringLiteral("download-history-table"));table_->setColumnCount(9);table_->setHorizontalHeaderLabels({tr("Name"),tr("Added"),tr("Completed"),tr("Removed"),tr("Size"),tr("Save location"),tr("Tags"),tr("Hash"),tr("V2 hash")});
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
  reload_->setObjectName(QStringLiteral("download-history-reload"));erase_->setObjectName(QStringLiteral("download-history-erase"));clear_->setObjectName(QStringLiteral("download-history-clear"));reset_->setObjectName(QStringLiteral("download-history-reset"));
  auto* buttons=new QHBoxLayout;buttons->addWidget(reload_);buttons->addStretch();buttons->addWidget(erase_);buttons->addWidget(clear_);buttons->addWidget(reset_);
  auto* layout=new QVBoxLayout(this);layout->addWidget(status_);layout->addWidget(table_);layout->addLayout(buttons);
  connect(reload_,&QPushButton::clicked,this,&DownloadHistoryPage::reload);connect(erase_,&QPushButton::clicked,this,&DownloadHistoryPage::erase);connect(clear_,&QPushButton::clicked,this,&DownloadHistoryPage::clear);connect(reset_,&QPushButton::clicked,this,&DownloadHistoryPage::reset);connect(table_,&QTableWidget::itemSelectionChanged,this,&DownloadHistoryPage::updateButtons);
  beginSession(0);
}
void DownloadHistoryPage::beginSession(quint64 session){session_=session;generation_=0;pending_.reset();shown_.reset();writable_=false;resetAvailable_=false;table_->setRowCount(0);status_->setText(session?tr("Reload saved download history."):tr("Open a profile to read download history."));updateButtons();}
void DownloadHistoryPage::invalidateSession(){beginSession(0);}
void DownloadHistoryPage::setAvailable(bool value){available_=value;updateButtons();}
void DownloadHistoryPage::setPending(DownloadHistoryCommand command){if(command.session!=session_||!command.request||pending_){return;}pending_=command;status_->setText(tr("Waiting for the profile worker…"));updateButtons();}
std::optional<DownloadHistoryPage::Key> DownloadHistoryPage::selected()const{
  const auto rows=table_->selectionModel()->selectedRows(0);if(rows.size()!=1||table_->currentRow()!=rows.front().row()){return {};}
  const auto* item=table_->item(rows.front().row(),0);if(!item){return {};}
  return Key{static_cast<DownloadHistorySide>(item->data(Qt::UserRole).toInt()),item->data(Qt::UserRole+1).toLongLong()};
}
void DownloadHistoryPage::updateButtons(){const bool ready=available_&&session_&&!pending_;reload_->setEnabled(ready);erase_->setEnabled(ready&&writable_&&selected().has_value());clear_->setEnabled(ready&&writable_&&table_->rowCount()>0);reset_->setEnabled(ready&&resetAvailable_);}
void DownloadHistoryPage::intent(DownloadHistoryCommand command){if(!available_||!session_||pending_||(command.kind!=DownloadHistoryKind::Reload&&!writable_)){return;}command.session=session_;command.generation=generation_;emit commandRequested(std::move(command));}
void DownloadHistoryPage::reload(){DownloadHistoryCommand request;request.kind=DownloadHistoryKind::Reload;intent(request);}
void DownloadHistoryPage::erase(){
  const auto key=selected();if(!key||!erase_->isEnabled()){return;}const auto generation=generation_;const auto shown=shown_;const QPointer<DownloadHistoryPage> guard(this);
  const auto answer=QMessageBox::question(this,tr("Remove history record"),tr("Remove this saved history record? Torrent and payload files will be kept."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||shown_!=shown||selected()!=key||!erase_->isEnabled()){return;}
  DownloadHistoryCommand request;request.kind=DownloadHistoryKind::Erase;request.side=key->side;request.uid=key->uid;intent(request);
}
void DownloadHistoryPage::clear(){
  if(!clear_->isEnabled()){return;}const auto generation=generation_;const auto shown=shown_;const QPointer<DownloadHistoryPage> guard(this);
  const auto answer=QMessageBox::question(this,tr("Clear saved history"),tr("Remove all saved active and removed history records? Torrent and payload files will be kept. This does not reset history from the current library."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||shown_!=shown||!clear_->isEnabled()){return;}
  DownloadHistoryCommand request;request.kind=DownloadHistoryKind::Clear;intent(request);
}
void DownloadHistoryPage::reset(){
  if(!reset_->isEnabled()){return;}const auto generation=generation_;const auto shown=shown_;const QPointer<DownloadHistoryPage> guard(this);
  const auto answer=QMessageBox::question(this,tr("Reset download history"),tr("Clear removed history and reconstruct active history from the complete stopped library? Torrent and payload files will be kept. No torrents will be started."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||shown_!=shown||!reset_->isEnabled()){return;}
  DownloadHistoryCommand request;request.kind=DownloadHistoryKind::Reset;intent(request);
}
void DownloadHistoryPage::refuse(const DownloadHistoryCommand& request){
  if(!pending_||pending_->session!=request.session||pending_->request!=request.request){return;}
  auto packet=std::make_shared<DownloadHistoryCommandPacket>();packet->session=request.session;packet->request=request.request;packet->kind=request.kind;packet->side=request.side;packet->uid=request.uid;packet->failure=tr("The profile worker did not accept this request. Reload before editing.");setResult(std::move(packet));
}
void DownloadHistoryPage::setResult(DownloadHistoryCommandPresentation packet){
  if(!packet||!pending_||packet->session!=session_||packet->request!=pending_->request||packet->kind!=pending_->kind||packet->side!=pending_->side||packet->uid!=pending_->uid){return;}
  pending_.reset();writable_=false;resetAvailable_=false;
  if(packet->status==DownloadHistoryStatus::Rejected||packet->status==DownloadHistoryStatus::Cancelled){generation_=0;status_->setText(packet->status==DownloadHistoryStatus::Cancelled?tr("Cancelled. Reload before editing."):tr("Unavailable: %1").arg(packet->failure.left(512)));updateButtons();return;}
  if(!packet->refreshFailure.isEmpty()){generation_=0;status_->setText(tr("Saved; reload to refresh: %1").arg(packet->refreshFailure.left(512)));updateButtons();return;}
  shown_=packet;generation_=packet->generation;const QSignalBlocker blocked(table_);table_->setSortingEnabled(false);table_->setRowCount(0);
  const auto append=[&](const std::shared_ptr<const core::SavedDownloadHistory>& records,DownloadHistorySide side){if(!records){return;}for(const auto& row:records->records){
    const auto at=table_->rowCount();table_->insertRow(at);QStringList tags;for(const auto& tag:row.tags){tags.push_back(text(tag));}
    const QStringList values{text(row.name),date(row.addTime),date(row.completeTime),date(row.removeTime),QString::number(row.size),text(row.saveLocation),tags.join(QStringLiteral(", ")),hashText(row.hash),hashText(row.hashV2)};
    for(int column=0;column<values.size();++column){auto* item=new QTableWidgetItem(values[column]);item->setToolTip(values[column]);if(column==0){item->setData(Qt::UserRole,static_cast<int>(side));item->setData(Qt::UserRole+1,QVariant::fromValue<qlonglong>(row.uid));}table_->setItem(at,column,item);}
  }};
  append(packet->active,DownloadHistorySide::Active);append(packet->dead,DownloadHistorySide::Dead);table_->setSortingEnabled(true);table_->sortItems(0,Qt::AscendingOrder);
  QString message=packet->enabled?tr("Download history enabled."):tr("Download history disabled. Existing records are retained.");
  if(packet->readState==DownloadHistoryReadState::Missing){message+=tr(" No saved history files exist.");}
  if(packet->review!=DownloadHistoryReview::None){message+=tr(" Preserved recovery or conflicting records require review; edits are disabled.");}
  else if(!packet->writable){message+=tr(" Read-only profile view.");}
  message+=tr(" %1 saved records. Hashes are informational.").arg(table_->rowCount());status_->setText(message);writable_=packet->writable;resetAvailable_=packet->resetAvailable;updateButtons();
}
} // namespace biglybtc::ui
