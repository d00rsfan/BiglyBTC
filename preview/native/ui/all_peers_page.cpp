// SPDX-License-Identifier: GPL-2.0-or-later
#include "all_peers_page.hpp"
#include "classic_sort_header.hpp"
#include <QApplication>
#include <QAction>
#include <QSignalBlocker>
#include <limits>
#include <QClipboard>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QRegularExpression>
#include <algorithm>
namespace biglybtc::ui {
namespace {
struct Column {const char* id;const char* label;int width;bool visible;};
constexpr Column columns[]={
 {"name","Torrent",250,true},{"localintf","Local interface",120,true},{"ip","IP",170,true},{"client","Client",150,true},{"T","Type",45,true},{"Protocol","Protocol",65,true},{"pieces","Pieces",150,true},{"%","Done",60,true},{"downloadspeed","Down speed",90,true},{"uploadspeed","Up speed",90,true},{"state","State",110,true},{"CountryCode","Country code",65,true},{"CountryFlag","Country flag",45,true},{"as","AS",220,true},
 {"port","Port",65,false},{"source","Source",100,false},{"Encryption","Encryption",85,false},{"download","Received",95,false},{"upload","Sent",95,false},{"I1","We are interested",95,false},{"I2","Peer is interested",95,false},{"C1","Peer chokes us",95,false},{"C2","We choke peer",95,false},{"S","Snubbed",70,false},{"optunchoke","Optimistic unchoke",100,false},{"incomingreqcount","Incoming requests",100,false},{"outgoingreqcount","Outgoing requests",100,false},{"latency","Latency",70,false},{"bytes-queued","Queued upload",95,false},{"gain","Gain",95,false},{"UpDownRatio","Up/Down ratio",90,false},{"network","Network",70,false},{"peer_id","Peer ID",180,false},{"Country","Country",140,false}
};
constexpr int Count=int(std::size(columns));
QString amount(qint64 value,bool rate=false){constexpr const char* units[]={"B","KiB","MiB","GiB","TiB","PiB","EiB"};double n=double(std::max<qint64>(0,value));int at=0;while(n>=1024&&at<6){n/=1024;++at;}return QString::number(n,'f',at?1:0)+' '+QString::fromLatin1(units[at])+(rate?QStringLiteral("/s"):QString{});}
QString endpoint(QString address,quint16 port){return (address.contains(':')?'['+address+']':address)+':'+QString::number(port);}
QString stateText(const PeerRow& row){return row.state==1?QObject::tr("Connecting"):row.state==2?QObject::tr("Handshaking"):row.state==3?QObject::tr("Connected"):QObject::tr("Unknown");}
QByteArray addressOrder(const QString& value){QHostAddress address(value);QByteArray bytes;if(address.protocol()==QAbstractSocket::IPv4Protocol){bytes.append(char(4));const auto v=address.toIPv4Address();for(int s=24;s>=0;s-=8){bytes.append(char(v>>s));}}else if(address.protocol()==QAbstractSocket::IPv6Protocol){bytes.append(char(6));const auto v=address.toIPv6Address();bytes.append(reinterpret_cast<const char*>(v.c),16);}return bytes;}
class PeerProxy final:public QSortFilterProxyModel {
public:using QSortFilterProxyModel::QSortFilterProxyModel;
protected:bool lessThan(const QModelIndex& a,const QModelIndex& b)const override{
 const auto x=sourceModel()->data(a,AllPeersModel::SortRole),y=sourceModel()->data(b,AllPeersModel::SortRole);
 if(x.metaType().id()==QMetaType::QByteArray){return x.toByteArray()<y.toByteArray();}
 if(x.metaType().id()==QMetaType::LongLong||x.metaType().id()==QMetaType::Int){return x.toLongLong()<y.toLongLong();}
 return QString::localeAwareCompare(x.toString(),y.toString())<0;
 }
};
class Availability final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
 void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
  QStyledItemDelegate::paint(painter,option,index);const auto bins=index.data(AllPeersModel::BinsRole).toByteArray();if(bins.size()!=128){return;}
  const auto rect=option.rect.adjusted(4,5,-4,-5);painter->save();painter->setClipRect(rect);
  for(int i=0;i<128;++i){const auto color=bins[i]==2?QColor(57,136,197):bins[i]==1?QColor(143,185,215):option.palette.alternateBase().color();painter->fillRect(QRect(rect.left()+rect.width()*i/128,rect.top(),std::max(1,rect.width()*(i+1)/128-rect.width()*i/128),rect.height()),color);}painter->restore();
 }
};
}
AllPeersModel::AllPeersModel(QObject* parent):QAbstractTableModel(parent){}
int AllPeersModel::rowCount(const QModelIndex& parent)const{return parent.isValid()||!packet_?0:int(packet_->rows.size());}
int AllPeersModel::columnCount(const QModelIndex& parent)const{return parent.isValid()?0:Count;}
QString AllPeersModel::columnId(int column){return column>=0&&column<Count?QString::fromLatin1(columns[column].id):QString{};}
int AllPeersModel::columnFor(QString id){for(int i=0;i<Count;++i){if(id==QLatin1StringView(columns[i].id)){return i;}}return -1;}
const PeerRow* AllPeersModel::row(int index)const{return packet_&&index>=0&&index<int(packet_->rows.size())?&packet_->rows[std::size_t(index)]:nullptr;}
QVariant AllPeersModel::headerData(int at,Qt::Orientation direction,int role)const{return direction==Qt::Horizontal&&role==Qt::DisplayRole&&at>=0&&at<Count?tr(columns[at].label):QVariant{};}
QVariant AllPeersModel::data(const QModelIndex& index,int role)const{
 const auto* r=row(index.row());if(!r||index.column()<0||index.column()>=Count){return {};}
 const auto id=columnId(index.column());
 if(role==Qt::ToolTipRole){return tr("%1\nRemote: %2\nLocal: %3\n%4; %5\nWe are interested: %6; peer chokes us: %7\nPeer is interested: %8; we choke peer: %9\nPayload received: %10; sent: %11\n%12")
  .arg(r->name,endpoint(r->address,r->port),endpoint(r->localAddress,r->localPort),stateText(*r),r->protocol,r->interesting?tr("Yes"):tr("No"),r->choked?tr("Yes"):tr("No"),r->remoteInterested?tr("Yes"):tr("No"),r->remoteChoked?tr("Yes"):tr("No"),amount(r->received),amount(r->sent),id=="as"?r->asnStatus:(!packet_->countryAvailable&&(id=="CountryCode"||id=="CountryFlag"||id=="Country")?tr("Country database is unavailable"):QString{}));}
 if(role==BinsRole){return id=="pieces"?r->availability:QVariant{};}if(role==ProgressRole){return r->progressPpm;}
 if(role==Qt::TextAlignmentRole){return (id=="name"||id=="ip"||id=="client"||id=="state"||id=="as"||id=="localintf")?int(Qt::AlignLeft|Qt::AlignVCenter):int(Qt::AlignRight|Qt::AlignVCenter);}
 if(role!=Qt::DisplayRole&&role!=SortRole){return {};}
 const bool sort=role==SortRole;
 if(id=="name"){return r->name;}if(id=="ip"){return sort?QVariant(addressOrder(r->address)):QVariant(r->address);}if(id=="localintf"){return sort?QVariant(addressOrder(r->localAddress)):QVariant(r->localInterface.isEmpty()?r->localAddress:r->localInterface);}
 if(id=="client"){return r->client;}if(id=="Protocol"){return r->protocol;}if(id=="state"){return sort?QVariant(r->state):QVariant(stateText(*r));}if(id=="T"){return r->incoming?QStringLiteral("R"):QStringLiteral("L");}
 if(id=="CountryCode"){return r->countryCode.isEmpty()?QStringLiteral("—"):r->countryCode;}if(id=="Country"){return r->countryName.isEmpty()?QStringLiteral("—"):r->countryName;}
 if(id=="CountryFlag"){if(r->countryCode.size()!=2){return QStringLiteral("—");}char32_t flag[]={char32_t(0x1f1e6+r->countryCode[0].toUpper().unicode()-'A'),char32_t(0x1f1e6+r->countryCode[1].toUpper().unicode()-'A')};return QString::fromUcs4(flag,2);}
 if(id=="as"){return r->asn.isEmpty()?QStringLiteral("—"):r->asn;}if(id=="source"){return r->source;}if(id=="Encryption"){return r->encryption;}if(id=="network"){return QStringLiteral("Public");}if(id=="peer_id"){return r->peerId;}
 if(id=="pieces"){return sort?QVariant(r->pieces):QVariant{};}
 if(id=="%"||id=="UpDownRatio"){if(id=="%"&&r->progressPpm<0){return sort?QVariant(-1):QVariant(QStringLiteral("—"));}const auto ratio=r->received>0?static_cast<long double>(r->sent)*1000/static_cast<long double>(r->received):-1;const qint64 value=id=="%"?r->progressPpm:ratio>=std::numeric_limits<qint64>::max()?std::numeric_limits<qint64>::max():static_cast<qint64>(ratio);return sort?QVariant(value):QVariant(id=="%"?QString::number(double(value)/10000,'f',1)+'%':value<0?QStringLiteral("—"):QString::number(double(value)/1000,'f',2));}
 qint64 value=0;bool rate=false,bytes=false,boolean=false;
 if(id=="downloadspeed"){value=r->downRate;rate=bytes=true;}else if(id=="uploadspeed"){value=r->upRate;rate=bytes=true;}else if(id=="download"){value=r->received;bytes=true;}else if(id=="upload"){value=r->sent;bytes=true;}else if(id=="gain"){value=r->received-r->sent;bytes=true;}
 else if(id=="port"){value=r->port;}else if(id=="latency"){value=r->rtt;}else if(id=="incomingreqcount"){value=r->uploadRequests;}else if(id=="outgoingreqcount"){value=r->downloadRequests;}else if(id=="bytes-queued"){value=r->queuedBytes;bytes=true;}
 else{boolean=true;if(id=="I1"){value=r->interesting;}else if(id=="I2"){value=r->remoteInterested;}else if(id=="C1"){value=r->choked;}else if(id=="C2"){value=r->remoteChoked;}else if(id=="S"){value=r->snubbed;}else if(id=="optunchoke"){value=r->optimistic;}}
 if(sort){return value;}if(boolean){return value?tr("Yes"):tr("No");}if(bytes){return value<0?'-'+amount(-value,rate):amount(value,rate);}return id=="latency"?QString::number(value)+QStringLiteral(" ms"):QString::number(value);
}
void AllPeersModel::beginSession(quint64 session){beginResetModel();session_=session;packet_.reset();endResetModel();}
bool AllPeersModel::apply(PeerPresentation packet){if(!packet||!session_||packet->session!=session_||!packet->sequence||(packet_&&packet->sequence<=packet_->sequence)){return false;}
 const bool same=packet_&&packet_->rows.size()==packet->rows.size()&&std::equal(packet_->rows.begin(),packet_->rows.end(),packet->rows.begin(),[](const auto& a,const auto& b){return a.key==b.key;});
 if(!same){beginResetModel();packet_=std::move(packet);endResetModel();}else{packet_=std::move(packet);if(rowCount()){emit dataChanged(index(0,0),index(rowCount()-1,Count-1));}}return true;}
AllPeersPage::AllPeersPage(quint64 session,QWidget* parent):QWidget(parent),session_(session),model_(new AllPeersModel(this)),proxy_(new PeerProxy(this)),table_(new QTableView(this)),status_(new QLabel(tr("Waiting for live peer connections…"),this)),filter_(new QLineEdit(this)),columns_(new QToolButton(this)){
 setObjectName(QStringLiteral("all-peers-page"));model_->beginSession(session);auto* layout=new QVBoxLayout(this);auto* bar=new QHBoxLayout;filter_->setPlaceholderText(tr("Filter peers or torrents"));filter_->setClearButtonEnabled(true);bar->addWidget(filter_,1);columns_->setText(tr("Columns"));auto* menu=new QMenu(columns_);columns_->setMenu(menu);columns_->setPopupMode(QToolButton::InstantPopup);bar->addWidget(columns_);layout->addLayout(bar);layout->addWidget(status_);layout->addWidget(table_,1);
 proxy_->setSourceModel(model_);proxy_->setSortRole(AllPeersModel::SortRole);proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);proxy_->setFilterKeyColumn(-1);proxy_->setDynamicSortFilter(true);table_->setModel(proxy_);table_->setHorizontalHeader(new ClassicSortHeader(table_));table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::ExtendedSelection);table_->setSortingEnabled(true);table_->setContextMenuPolicy(Qt::CustomContextMenu);table_->horizontalHeader()->setSectionsMovable(true);table_->horizontalHeader()->setMinimumSectionSize(17);table_->verticalHeader()->hide();table_->setAlternatingRowColors(true);table_->setItemDelegateForColumn(AllPeersModel::columnFor("pieces"),new Availability(table_));
 for(int i=0;i<Count;++i){table_->setColumnWidth(i,columns[i].width);table_->setColumnHidden(i,!columns[i].visible);auto* action=menu->addAction(tr(columns[i].label));action->setCheckable(true);action->setChecked(columns[i].visible);action->setProperty("column",i);connect(action,&QAction::toggled,this,[this,i](bool visible){table_->setColumnHidden(i,!visible);});}
 table_->sortByColumn(AllPeersModel::columnFor("uploadspeed"),Qt::DescendingOrder);connect(filter_,&QLineEdit::textChanged,this,[this](const QString& text){proxy_->setFilterFixedString(text);updateSummary();});connect(table_,&QTableView::customContextMenuRequested,this,&AllPeersPage::popup);
 connect(table_,&QTableView::doubleClicked,this,[this](const QModelIndex& index){const auto original=proxy_->mapToSource(index);if(const auto* row=model_->row(original.row())){emit openTorrent(row->hash);}});
}
void AllPeersPage::applyLayout(const core::SavedTableLayout& layout){
 if(layout.hasColumnSettings){std::vector<std::pair<int,int>> order;
  for(const auto& saved:layout.columns){const auto logical=AllPeersModel::columnFor(QString::fromStdString(saved.id.bytes));if(logical<0){continue;}if(saved.visibility.value){table_->setColumnHidden(logical,saved.visibility.value->value!=1);}if(saved.width.value){table_->setColumnWidth(logical,std::clamp(saved.width.value->value,17,4096));}if(saved.position.value&&saved.position.value->value>=0){order.push_back({saved.position.value->value,logical});}}
  std::ranges::stable_sort(order);auto* header=table_->horizontalHeader();for(int at=0;at<int(order.size());++at){header->moveSection(header->visualIndex(order[std::size_t(at)].second),at);}
 }
 for(const auto& sort:layout.sort){if(!sort.id.value){continue;}const auto id=QString::fromStdString(sort.id.value->bytes);const auto logical=AllPeersModel::columnFor(id);if(logical<0){continue;}auto direction=Qt::AscendingOrder;for(const auto& saved:layout.columns){if(saved.id.bytes==sort.id.value->bytes&&saved.sortOrder.value){direction=saved.sortOrder.value->value==0?Qt::DescendingOrder:Qt::AscendingOrder;break;}}table_->sortByColumn(logical,direction);break;}
 for(auto* action:columns_->menu()->actions()){const auto logical=action->property("column").toInt();const QSignalBlocker block(action);action->setChecked(!table_->isColumnHidden(logical));}
}
void AllPeersPage::setSnapshot(PeerPresentation packet){if(!packet||packet->session!=session_){return;}QString selected;if(const auto rows=table_->selectionModel()->selectedRows();!rows.empty()){if(const auto* row=model_->row(proxy_->mapToSource(rows.front()).row())){selected=row->key;}}const auto scroll=table_->verticalScrollBar()->value();const auto horizontal=table_->horizontalScrollBar()->value();
 if(!model_->apply(packet)){return;}if(!layoutApplied_&&packet->layout){applyLayout(*packet->layout);layoutApplied_=true;}
 if(!selected.isEmpty()){for(int i=0;i<model_->rowCount();++i){if(model_->row(i)->key==selected){table_->selectRow(proxy_->mapFromSource(model_->index(i,0)).row());break;}}}table_->verticalScrollBar()->setValue(scroll);table_->horizontalScrollBar()->setValue(horizontal);updateSummary();}
void AllPeersPage::updateSummary(){const auto p=model_->packet();if(!p){return;}if(!p->available){status_->setText(p->failure.isEmpty()?tr("No active peer session"):p->failure);return;}
 status_->setText(tr("%1 peers · %2 downloading · %3 seeding · %4 queued · %5 checking · %6 errors%7%8")
  .arg(proxy_->rowCount()).arg(p->downloading).arg(p->seeding).arg(p->queued).arg(p->checking).arg(p->failed).arg(p->prepared<p->preparationTotal?tr(" · Opening library %1/%2").arg(p->prepared).arg(p->preparationTotal):QString{}).arg(p->truncated?tr(" · Peer display limit reached"):QString{}));
 if(p->unavailable){status_->setText(status_->text()+tr(" · %1 peer samples unavailable").arg(p->unavailable));}
 if(p->sessionReceived&&p->sessionSent){status_->setText(status_->text()+tr(" · Session payload ↓ %1 ↑ %2").arg(amount(*p->sessionReceived),amount(*p->sessionSent)));}}
void AllPeersPage::invalidateSession(){session_=0;model_->beginSession(0);status_->setText(tr("Session ended"));setEnabled(false);}
void AllPeersPage::refresh(){emit refreshRequested();}
void AllPeersPage::popup(const QPoint& position){const auto indexes=table_->selectionModel()->selectedRows();if(indexes.empty()){return;}QStringList addresses,endpoints;QString hash;for(const auto& index:indexes){if(const auto* row=model_->row(proxy_->mapToSource(index).row())){addresses.push_back(row->address);endpoints.push_back(endpoint(row->address,row->port));if(hash.isEmpty()){hash=row->hash;}}}QMenu menu(this);auto* address=menu.addAction(tr("Copy IP address"));auto* endpointAction=menu.addAction(tr("Copy endpoint"));auto* torrent=menu.addAction(tr("Open torrent"));const auto selected=menu.exec(table_->viewport()->mapToGlobal(position));if(selected==address){QApplication::clipboard()->setText(addresses.join('\n'));}else if(selected==endpointAction){QApplication::clipboard()->setText(endpoints.join('\n'));}else if(selected==torrent&&!hash.isEmpty()){emit openTorrent(hash);}}
}
