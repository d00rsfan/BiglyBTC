// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrent_detail_pages.hpp"
#include "all_peers_page.hpp"
#include "table_viewport_guard.hpp"
#include "transfer_duration.hpp"
#include <QAbstractTableModel>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QFormLayout>
#include <deque>
#include <array>
#include <algorithm>
#include <cmath>
namespace biglybtc::ui {
namespace {QString text(const std::string& value){return QString::fromUtf8(value.data(),qsizetype(value.size()));}
QString yes(bool value){return value?QObject::tr("Yes"):QObject::tr("No");}
using Rows=std::vector<QVariantList>;
}
void applyTorrentTableLayout(QTableView* table,const core::SavedTableLayout& layout,const QStringList& ids){
 if(!table){return;}TableViewportGuard guard(table);auto* header=table->horizontalHeader();header->setSectionsMovable(true);
 if(layout.hasColumnSettings){std::vector<std::pair<int,int>> order;for(const auto& column:layout.columns){const int logical=int(ids.indexOf(QString::fromStdString(column.id.bytes)));if(logical<0){continue;}if(column.visibility.value){table->setColumnHidden(logical,column.visibility.value->value!=1);}if(column.width.value){header->setSectionResizeMode(logical,QHeaderView::Interactive);table->setColumnWidth(logical,std::clamp(column.width.value->value,17,4096));}if(column.position.value&&column.position.value->value>=0){order.emplace_back(column.position.value->value,logical);}}std::ranges::stable_sort(order);for(int n=0;n<int(order.size());++n){header->moveSection(header->visualIndex(order[std::size_t(n)].second),n);}}
 for(const auto& saved:layout.sort){if(!saved.id.value){continue;}const int logical=int(ids.indexOf(QString::fromStdString(saved.id.value->bytes)));if(logical<0){continue;}auto direction=Qt::AscendingOrder;for(const auto& column:layout.columns){if(column.id.bytes==saved.id.value->bytes&&column.sortOrder.value){direction=column.sortOrder.value->value==0?Qt::DescendingOrder:Qt::AscendingOrder;break;}}table->sortByColumn(logical,direction);break;}
}
class InspectionTableModel final:public QAbstractTableModel {
public:
 InspectionTableModel(QStringList headers,QObject* parent):QAbstractTableModel(parent),headers_(std::move(headers)){}
 int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(rows_.size());}
 int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(headers_.size());}
 QVariant headerData(int n,Qt::Orientation orientation,int role)const override{return orientation==Qt::Horizontal&&role==Qt::DisplayRole&&n>=0&&n<headers_.size()?headers_[n]:QVariant{};}
 QVariant data(const QModelIndex& index,int role)const override{if(!index.isValid()||index.row()<0||index.row()>=int(rows_.size())||index.column()<0||index.column()>=int(rows_[std::size_t(index.row())].size())){return {};}
  const auto& value=rows_[std::size_t(index.row())][index.column()];if(role==Qt::UserRole){return value;}
  if(role==Qt::ToolTipRole&&table_&&table_->objectName()=="torrent-trackers"&&index.column()==4){const auto& row=rows_[std::size_t(index.row())];QStringList detail{value.toString()};for(const int column:{2,3,11}){if(column<row.size()&&!row[column].toString().isEmpty()){detail.push_back(row[column].toString());}}return detail.join('\n');}

  if(role==Qt::DisplayRole||role==Qt::ToolTipRole||role==Qt::AccessibleTextRole){return value;}return {};
 }
 void apply(Rows rows){bool same=rows_.size()==rows.size();for(std::size_t i=0;same&&i<rows.size();++i){same=!rows[i].empty()&&!rows_[i].empty()&&rows[i][0]==rows_[i][0];}
  TableViewportGuard guard(table_);if(!same){beginResetModel();}rows_=std::move(rows);if(!same){endResetModel();}else if(!rows_.empty()){emit dataChanged(index(0,0),index(rowCount()-1,columnCount()-1));}
 }
 void bind(QTableView* table){table_=table;}
private:QStringList headers_;Rows rows_;QPointer<QTableView> table_;
};
class TorrentPieceMap final:public QWidget {
public:
 explicit TorrentPieceMap(QWidget* parent):QWidget(parent){setMinimumSize(200,130);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setToolTip(tr("Green: verified locally. Grey: missing. Blue: present at connected peers. Unknown local state is shown separately. The displayed range is bounded."));}
 void apply(const engine::TorrentInspection& value){bits_=value.pieces;availability_=value.pieceAvailability;known_=value.piecesKnown;updateHeight();update();}
 void resizeEvent(QResizeEvent*)override{updateHeight();}
 void updateHeight(){const int columns=std::max(1,width()/8);setMinimumHeight(std::max(130,8*((int(std::max(bits_.size(),availability_.size()))+columns-1)/columns)));}
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),palette().base());if(!known_&&availability_.empty()){p.drawText(rect(),Qt::AlignCenter,tr("Piece state is not classified by the current owner"));return;}
  const int count=int(std::max(bits_.size(),availability_.size()));const int columns=std::max(1,width()/8);for(int i=0;i<count;++i){const int y=(i/columns)*8;if(y>=height()){break;}QColor colour=palette().mid().color();if(known_&&i<int(bits_.size())&&bits_[std::size_t(i)]){colour=QColor(40,165,75);}else if(i<int(availability_.size())&&availability_[std::size_t(i)]>0){colour=QColor(70,130,200);}p.fillRect((i%columns)*8,y,7,7,colour);}
 }
 QSize sizeHint()const override{return {800,400};}
private:std::vector<std::uint8_t> bits_;std::vector<int> availability_;bool known_=false;
};
class TorrentActivityPlot final:public QWidget {
 struct Point{qint64 time,down,up;};std::deque<Point> points_;
public:
 explicit TorrentActivityPlot(QWidget* parent):QWidget(parent){setMinimumSize(200,140);setToolTip(tr("Actual payload rates sampled while this torrent's properties are observed. Blue: download. Green: upload. Up to 30 minutes are retained in memory."));}
 void clear(){points_.clear();update();}
 void sample(qint64 time,qint64 down,qint64 up){if(!points_.empty()&&time<=points_.back().time){return;}points_.push_back({time,down,up});while(points_.size()>900||(!points_.empty()&&time-points_.front().time>1800000)){points_.pop_front();}update();}
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),palette().base());if(points_.size()<2){p.drawText(rect(),Qt::AlignCenter,tr("Collecting actual payload rate samples…"));return;}qint64 high=1;for(const auto& v:points_){high=std::max({high,v.down,v.up});}const auto area=rect().adjusted(65,15,-10,-30);p.setPen(palette().text().color());p.drawText(3,20,transferBytes(high,{},true));p.drawText(65,height()-5,tr("Download / Upload — observed history"));
  const auto duration=std::max<qint64>(1,points_.back().time-points_.front().time);for(int line=0;line<2;++line){QPolygonF polygon;for(const auto& v:points_){polygon<<QPointF(area.left()+double(v.time-points_.front().time)*area.width()/duration,area.bottom()-double(line?v.up:v.down)*area.height()/high);}p.setPen(QPen(line?QColor(35,160,75):QColor(60,120,210),1.5));p.drawPolyline(polygon);}
 }
};
class TorrentSwarmPlot final:public QWidget {
 PeerPresentation packet_;
public:
 explicit TorrentSwarmPlot(QWidget* parent):QWidget(parent){setMinimumSize(200,160);setToolTip(tr("Actual selected-torrent connections. Blue: download payload. Green: upload payload. Grey: idle. Up to 250 connections are drawn; the Peers table lists the full bounded snapshot."));}
 void apply(PeerPresentation packet){packet_=std::move(packet);update();}
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());if(!packet_||!packet_->available){p.drawText(rect(),Qt::AlignCenter,tr("No live peer snapshot"));return;}
  const QPointF centre(width()/2.,height()/2.);const double radius=std::max(20.,std::min(width(),height())*.36);const auto count=std::min<std::size_t>(250,packet_->rows.size());
  for(std::size_t n=0;n<count;++n){const auto& peer=packet_->rows[n];const double angle=double(n)*6.283185307179586/std::max<std::size_t>(1,count);const QPointF end=centre+QPointF(std::cos(angle)*radius,std::sin(angle)*radius);const QColor colour=peer.upRate>0?QColor(35,160,75):peer.downRate>0?QColor(60,120,210):palette().mid().color();p.setPen(QPen(colour,peer.upRate>0||peer.downRate>0?2:1));p.drawLine(centre,end);p.setBrush(peer.seed?QColor(230,180,50):palette().base());p.drawEllipse(end,4,4);}
  p.setPen(palette().text().color());p.setBrush(palette().base());p.drawEllipse(centre,25,25);p.drawText(QRectF(centre-QPointF(25,25),QSizeF(50,50)),Qt::AlignCenter,tr("You"));p.drawText(QRect(0,0,width(),30),Qt::AlignCenter,tr("%1 connected peers%2").arg(packet_->rows.size()).arg(packet_->rows.size()>250?tr(" — first 250 shown"):QString{}));
 }
};
TorrentDetailPages::TorrentDetailPages(QTabWidget* tabs):QObject(tabs),tabs_(tabs){
 // Every long form lives behind a scroll area; inactive pages cannot enlarge the main window.
 const auto generalPage=tabs->widget(0);tabs->removeTab(0);auto* generalScroll=new QScrollArea(tabs);generalScroll->setWidgetResizable(true);generalScroll->setFrameShape(QFrame::NoFrame);generalScroll->setWidget(generalPage);tabs->insertTab(0,generalScroll,tr("General"));
 auto table=[&](QWidget* parent,QString id,QStringList headers){auto* view=new QTableView(parent);view->setObjectName(id);auto* model=new InspectionTableModel(std::move(headers),view);model->bind(view);auto* proxy=new QSortFilterProxyModel(view);proxy->setSourceModel(model);proxy->setSortRole(Qt::UserRole);view->setModel(proxy);view->setSortingEnabled(true);view->setSelectionBehavior(QAbstractItemView::SelectRows);view->setAlternatingRowColors(true);view->verticalHeader()->hide();view->horizontalHeader()->setStretchLastSection(true);view->setMinimumSize(120,100);return std::pair{view,model};};
 auto [general,gm]=table(generalPage,"torrent-general-facts",{tr("Property"),tr("Value")});general_=gm;general->setMinimumHeight(180);if(auto* layout=qobject_cast<QFormLayout*>(generalPage->layout())){layout->addRow(general);}
 auto page=[&](QString name,engine::InspectionSection section){auto* value=new QWidget(tabs);value->setProperty("inspection-section",int(section));value->setObjectName("torrent-details-"+name.toLower().replace(' ','-'));auto* layout=new QVBoxLayout(value);layout->setContentsMargins(4,4,4,4);tabs->addTab(value,name);return value;};
 auto tablePage=[&](QString name,engine::InspectionSection section,QString id,QStringList headers){auto* value=page(name,section);auto [view,model]=table(value,id,std::move(headers));value->layout()->addWidget(view);return model;};
 generalScroll->setProperty("inspection-section",int(engine::InspectionSection::General));tabs->widget(1)->setProperty("inspection-section",int(engine::InspectionSection::Files));
 trackers_=tablePage(tr("Trackers"),engine::InspectionSection::Trackers,"torrent-trackers",{tr("URL"),tr("Tier"),tr("Local endpoint"),tr("Protocol"),tr("Status"),tr("Seeds"),tr("Leechers"),tr("Completed"),tr("Peers returned"),tr("Failures"),tr("Next announce (s)"),tr("Response")});
 if(auto* trackerTable=tabs->findChild<QTableView*>("torrent-trackers")){auto* header=trackerTable->horizontalHeader();trackerTable->setColumnWidth(0,350);trackerTable->setColumnWidth(4,130);trackerTable->setColumnWidth(2,200);const std::array order{0,4,8,5,6,7,10,2,3,1,9,11};for(int n=0;n<int(order.size());++n){header->moveSection(header->visualIndex(order[std::size_t(n)]),n);}}
 auto* trackerPage=tabs->widget(2);auto* trackerButtons=new QHBoxLayout;reannounce_=new QPushButton(tr("Update tracker"),trackerPage);reannounce_->setObjectName("torrent-tracker-update");scrape_=new QPushButton(tr("Request scrape"),trackerPage);scrape_->setObjectName("torrent-tracker-scrape");trackerButtons->addWidget(reannounce_);trackerButtons->addWidget(scrape_);trackerButtons->addStretch();qobject_cast<QVBoxLayout*>(trackerPage->layout())->addLayout(trackerButtons);reannounce_->setEnabled(false);scrape_->setEnabled(false);connect(reannounce_,&QPushButton::clicked,this,[this]{emit trackerRequested(false);});connect(scrape_,&QPushButton::clicked,this,[this]{emit trackerRequested(true);});
 auto* peerPage=page(tr("Peers"),engine::InspectionSection::Peers);peers_=new AllPeersPage(0,peerPage);peerPage->layout()->addWidget(peers_);
 pieces_=tablePage(tr("Pieces"),engine::InspectionSection::Pieces,"torrent-pieces",{tr("Piece"),tr("Blocks"),tr("Finished"),tr("Writing"),tr("Requested")});
 auto* mapPage=page(tr("Piece Map"),engine::InspectionSection::Map);auto* mapLayout=qobject_cast<QVBoxLayout*>(mapPage->layout());auto* range=new QHBoxLayout;range->addWidget(new QLabel(tr("First piece (up to 65,536 per range):"),mapPage));first_=new QSpinBox(mapPage);first_->setObjectName("torrent-piece-range");first_->setRange(0,2147483647);first_->setSingleStep(65536);range->addWidget(first_);range->addStretch();mapLayout->addLayout(range);auto* legend=new QLabel(tr("Green: verified · Grey: missing · Blue: available from peers · Unknown verification stays unknown"),mapPage);legend->setWordWrap(true);mapLayout->addWidget(legend);map_=new TorrentPieceMap(mapPage);auto* mapScroll=new QScrollArea(mapPage);mapScroll->setWidgetResizable(true);mapScroll->setWidget(map_);mapLayout->addWidget(mapScroll,1);connect(first_,&QSpinBox::valueChanged,this,&TorrentDetailPages::interestChanged);
 blocks_=tablePage(tr("Piece Blocks"),engine::InspectionSection::Blocks,"torrent-blocks",{tr("Piece"),tr("Block"),tr("State"),tr("Received"),tr("Size"),tr("Requests"),tr("Peer")});
 auto* swarmPage=page(tr("Swarm"),engine::InspectionSection::Swarm);swarm_=new TorrentSwarmPlot(swarmPage);swarmPage->layout()->addWidget(swarm_);
 auto* activityPage=page(tr("Activity"),engine::InspectionSection::Activity);activity_=new TorrentActivityPlot(activityPage);activityPage->layout()->addWidget(activity_);
 information_=tablePage(tr("Torrent Information"),engine::InspectionSection::Information,"torrent-information",{tr("Property"),tr("Value")});
 privacy_=tablePage(tr("Privacy"),engine::InspectionSection::Privacy,"torrent-privacy",{tr("Property"),tr("Current owner")});
 console_=tablePage(tr("Console"),engine::InspectionSection::Console,"torrent-console",{tr("Time"),tr("Event")});
 transfers_=tablePage(tr("Transfer Statistics"),engine::InspectionSection::Transfers,"torrent-transfer-statistics",{tr("Counter"),tr("Value")});
 auto* tagsPage=page(tr("Tags"),engine::InspectionSection::Tags);auto [tagTable,tm]=table(tagsPage,"torrent-tags",{tr("Assigned manual tag")});tagsModel_=tm;tagsPage->layout()->addWidget(tagTable);auto* manage=new QPushButton(tr("Manage tags…"),tagsPage);tagsPage->layout()->addWidget(manage);connect(manage,&QPushButton::clicked,this,&TorrentDetailPages::tagsRequested);
 auto* optionsPage=page(tr("Options"),engine::InspectionSection::Options);auto* optionsForm=new QWidget(optionsPage);auto* optionsLayout=new QFormLayout(optionsForm);
 auto spin=[&](QString label,QString id,int max){auto* result=new QSpinBox(optionsForm);result->setObjectName(id);result->setRange(0,max);result->setSpecialValueText(tr("Default / unlimited"));optionsLayout->addRow(label,result);connect(result,&QSpinBox::valueChanged,this,[this]{optionsDirty_=true;});return result;};
 download_=spin(tr("Download limit (bytes/s):"),"torrent-download-limit",2147483647);upload_=spin(tr("Upload limit (bytes/s):"),"torrent-upload-limit",2147483647);connections_=spin(tr("Maximum connections:"),"torrent-connection-limit",65535);slots_=spin(tr("Upload slots:"),"torrent-upload-slots",65535);slots_->setMinimum(2);slots_->setSpecialValueText({});
 auto* optionsNote=new QLabel(tr("Saved in the original torrent configuration. Stop this torrent before applying changes."),optionsForm);optionsNote->setWordWrap(true);optionsLayout->addRow(optionsNote);applyOptions_=new QPushButton(tr("Apply"),optionsForm);applyOptions_->setEnabled(false);optionsLayout->addRow(applyOptions_);applyOptions_->setText(tr("Apply rate limits"));connect(applyOptions_,&QPushButton::clicked,this,[this]{emit optionsRequested(true,download_->value(),upload_->value());});
 applyConnections_=new QPushButton(tr("Apply connection limits"),optionsForm);applyConnections_->setEnabled(false);optionsLayout->addRow(applyConnections_);connect(applyConnections_,&QPushButton::clicked,this,[this]{emit optionsRequested(false,connections_->value(),slots_->value());});auto* optionsScroll=new QScrollArea(optionsPage);optionsScroll->setWidgetResizable(true);optionsScroll->setWidget(optionsForm);optionsPage->layout()->addWidget(optionsScroll);
 chat_=new ChatPage(tabs);chat_->setObjectName(QStringLiteral("torrent-details-chat"));chat_->setProperty("inspection-section",int(engine::InspectionSection::General));chat_->setFixedRoom({});tabs->addTab(chat_,tr("Chat"));auto* reconnect=new QPushButton(tr("Connect to download chat"),chat_);reconnect->setObjectName(QStringLiteral("torrent-chat-connect"));qobject_cast<QVBoxLayout*>(chat_->layout())->insertWidget(0,reconnect,0,Qt::AlignLeft);connect(reconnect,&QPushButton::clicked,this,[this]{if(downloadSession_&&!downloadHash_.isEmpty()){emit downloadChatRequested(downloadSession_,downloadHash_);}});
 status_=new QLabel(tabs);status_->setObjectName("torrent-inspection-status");status_->setWordWrap(true);status_->setMaximumWidth(600);tabs->setCornerWidget(status_,Qt::TopRightCorner);
 connect(tabs,&QTabWidget::currentChanged,this,&TorrentDetailPages::interestChanged);
}
bool TorrentDetailPages::chatVisible()const{return tabs_->currentWidget()==chat_;}
QString TorrentDetailPages::readingRoom()const{return chatVisible()?chat_->joinedRoom():QString{};}
void TorrentDetailPages::setDownloadContext(quint64 session,QString hash){
 if(downloadSession_!=session||downloadHash_!=hash){downloadSession_=session;downloadHash_=std::move(hash);chatRequested_=false;chat_->setFixedRoom({});ChatViewSnapshot empty;empty.generation=session;empty.availability=tr("Opening download chat…");chat_->setSnapshot(std::move(empty));}
 if(session&&!downloadHash_.isEmpty()&&chatVisible()&&!chatRequested_){chatRequested_=true;emit downloadChatRequested(session,downloadHash_);}
}
void TorrentDetailPages::setDownloadChat(quint64 session,QString hash,ChatViewSnapshot value){if(session!=downloadSession_||hash!=downloadHash_){return;}chat_->setFixedRoom(value.selected);chat_->setSnapshot(std::move(value));}
engine::InspectionSection TorrentDetailPages::section()const {return static_cast<engine::InspectionSection>(tabs_->currentWidget()->property("inspection-section").toInt());}
int TorrentDetailPages::firstPiece()const{return section()==engine::InspectionSection::Map?first_->value():0;}
void TorrentDetailPages::beginInterest(const TorrentDetailInterest& interest){const bool changed=interest.hash!=interest_.hash||interest.session!=interest_.session;interest_=interest;sequence_=0;peers_->beginSession(interest.session);status_->setText(interest.hash.isEmpty()?QString{}:tr("Reading current torrent…"));reannounce_->setEnabled(false);scrape_->setEnabled(false);if(changed){peers_->invalidateSession();peers_->beginSession(interest.session);swarm_->apply({});admission_=0;optionsDirty_=false;observedHash_.clear();activity_->clear();for(auto* model:{general_,trackers_,pieces_,blocks_,information_,privacy_,transfers_,console_,tagsModel_}){model->apply({});}map_->apply({});}}
void TorrentDetailPages::setTags(const QStringList& tags){Rows rows;for(const auto& tag:tags){rows.push_back({tag});}tagsModel_->apply(std::move(rows));}
void TorrentDetailPages::setOptionsWritable(bool value){applyOptions_->setEnabled(value);applyConnections_->setEnabled(value);}
void TorrentDetailPages::trackerFinished(QString failure){status_->setText(failure.isEmpty()?tr("Tracker request queued; awaiting a real response"):failure);}
void TorrentDetailPages::optionsFinished(QString failure){if(failure.isEmpty()){optionsDirty_=false;}status_->setText(failure.isEmpty()?tr("Original torrent options saved"):failure);}
bool TorrentDetailPages::apply(TorrentDetailPresentation packet){
 if(!packet||packet->interest!=interest_||packet->sequence<=sequence_||packet->actual.hash!=interest_.hash.toStdString()){return false;}sequence_=packet->sequence;const auto& a=packet->actual;
 if(layoutSession_!=interest_.session){layoutSession_=interest_.session;layoutApplied_={};}
 if(packet->layout&&interest_.section==engine::InspectionSection::Trackers&&!layoutApplied_[0]){auto* view=tabs_->findChild<QTableView*>("torrent-trackers");applyTorrentTableLayout(view,*packet->layout,{"name","native.tier","native.local","native.protocol","status","seeds","leechers","completed","peers","native.failures","updatein","native.response"});layoutApplied_[0]=true;}
 if(packet->layout&&interest_.section==engine::InspectionSection::Pieces&&!layoutApplied_[1]){auto* view=tabs_->findChild<QTableView*>("torrent-pieces");applyTorrentTableLayout(view,*packet->layout,{"number","blocks","completed","native.writing","native.requested"});layoutApplied_[1]=true;}

 if(observedHash_!=interest_.hash||(a.admission&&admission_!=a.admission)||(packet->epoch&&epoch_!=packet->epoch)){if(packet->epoch){epoch_=packet->epoch;}observedHash_=interest_.hash;if(a.admission){admission_=a.admission;}activity_->clear();optionsDirty_=false;}
 status_->setText(!packet->failure.isEmpty()?packet->failure:!a.error.empty()?text(a.error):a.truncated?tr("Bounded snapshot — some data omitted"):a.retained?tr("Live torrent data"):tr("Saved torrent — no active owner"));
 const bool trackersActive=a.retained&&a.checked&&!a.paused&&a.error.empty()&&a.trackersAllowed;reannounce_->setEnabled(trackersActive);scrape_->setEnabled(trackersActive);
 if(a.retained){activity_->sample(packet->observedMillis,a.downloadRate,a.uploadRate);}
 general_->apply({{tr("Piece length"),transferBytes(a.pieceLength)},{tr("Pieces"),a.totalPieces},{tr("Connected peers / seeds"),QString::number(a.connections)+" / "+QString::number(a.seeds)},{tr("Download rate"),a.retained?transferBytes(a.downloadRate,{},true):tr("Unavailable")},{tr("Upload rate"),a.retained?transferBytes(a.uploadRate,{},true):tr("Unavailable")},{tr("Availability"),a.retained?QString::number(a.availability,'f',3):tr("Unavailable")},{tr("Force start"),yes(a.forceStart)},{tr("Original resume decision"),text(a.resumeDecision)}});
 switch(interest_.section){
 case engine::InspectionSection::Trackers:{Rows rows;for(const auto& t:a.trackers){const QString state=t.state==2?tr("Announce OK"):t.state==1?tr("Announcing"):t.state==3?tr("Error / skipped"):tr("Not observed");auto count=[](int v)->QVariant{return v<0?QVariant{}:QVariant{v};};rows.push_back({text(t.url),t.tier,text(t.local),text(t.protocol),state,count(t.seeds),count(t.leechers),count(t.completed),count(t.peers),t.failures,count(t.nextSeconds),text(t.message)});}trackers_->apply(std::move(rows));break;}
 case engine::InspectionSection::Swarm:swarm_->apply(packet->peers);break;
 case engine::InspectionSection::Peers:if(packet->peers){peers_->setSnapshot(packet->peers);}break;
 case engine::InspectionSection::Pieces:{Rows rows;for(const auto& p:a.activePieces){rows.push_back({p.index,p.blocks,p.finished,p.writing,p.requested});}pieces_->apply(std::move(rows));break;}
 case engine::InspectionSection::Map:map_->apply(a);break;
 case engine::InspectionSection::Blocks:{Rows rows;for(const auto& b:a.blocks){rows.push_back({b.piece,b.index,QString::fromLatin1(std::array{"Missing","Requested","Writing","Finished"}[std::size_t(std::clamp(b.state,0,3))]),b.received,b.size,b.requests,text(b.peer)});}blocks_->apply(std::move(rows));break;}
 case engine::InspectionSection::Information:information_->apply({{tr("Name"),text(a.name)},{tr("Original identity"),text(a.hash)},{tr("V1 info hash"),text(a.v1)},{tr("V2 info hash"),text(a.v2)},{tr("Size"),transferBytes(a.bytes)},{tr("Piece length"),transferBytes(a.pieceLength)},{tr("Pieces"),a.totalPieces},{tr("Private torrent"),yes(a.privateTorrent)},{tr("Creator"),text(a.creator)},{tr("Created"),a.created?QDateTime::fromSecsSinceEpoch(a.created).toString(Qt::ISODate):QString{}},{tr("Comment"),text(a.comment)}});break;
 case engine::InspectionSection::Privacy:privacy_->apply({{tr("Private torrent"),yes(a.privateTorrent)},{tr("Mainline DHT allowed for this admission"),a.retained?yes(a.dht):tr("No active owner")},{tr("Peer exchange allowed"),a.retained?yes(a.pex):tr("No active owner")},{tr("Local discovery allowed"),a.retained?yes(a.lsd):tr("No active owner")},{tr("Tracker metadata present"),yes(a.trackersAllowed)},{tr("Anonymous transport"),tr("Public connections do not provide anonymity")}});break;
 case engine::InspectionSection::Transfers:transfers_->apply({{tr("Overall received (saved + current)"),a.cumulativeReceived?transferBytes(*a.cumulativeReceived):tr("Unavailable")},{tr("Overall sent (saved + current)"),a.cumulativeSent?transferBytes(*a.cumulativeSent):tr("Unavailable")},{tr("Received in this admission"),a.retained?transferBytes(a.received):tr("Unavailable")},{tr("Sent in this admission"),a.retained?transferBytes(a.sent):tr("Unavailable")},{tr("Verified completed bytes"),a.checked?transferBytes(a.completed):tr("Not classified")},{tr("Discarded"),transferBytes(a.discarded)},{tr("Hash failed"),transferBytes(a.hashFailed)},{tr("Active time in this admission"),transferUptime(a.activeSeconds)},{tr("Seeding time in this admission"),transferUptime(a.seedingSeconds)}});break;
 case engine::InspectionSection::Console:{Rows rows;for(const auto& event:a.events){rows.push_back({QDateTime::fromMSecsSinceEpoch(event.wallMillis).toString(Qt::ISODateWithMs),text(event.message)});}console_->apply(std::move(rows));if(a.events.empty()&&a.retained){status_->setText(tr("Listening for actual engine events; up to 512 are retained"));}break;}
 case engine::InspectionSection::Options:if(!optionsDirty_){const QSignalBlocker d(download_),u(upload_),c(connections_),s(slots_);download_->setValue(packet->savedOptions[0]);upload_->setValue(packet->savedOptions[1]);connections_->setValue(packet->savedOptions[2]);slots_->setValue(packet->savedOptions[3]);}break;
 default:break;
 }return true;
}
}
