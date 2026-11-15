// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discoveries_page.hpp"
#include "classic_sort_header.hpp"
#include "biglybtc/bencode.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QScrollBar>
#include <QAbstractTableModel>
#include <QFont>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <limits>
namespace biglybtc::ui {
namespace {
QString q(std::string_view s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string n(qint64 v){return "i"+std::to_string(v)+"e";}
struct Column {const char* id;const char* title;int width;bool visible;};
constexpr std::array columns{
 Column{"rc_new","New",48,true},Column{"rc_rank","Rank",55,true},Column{"rc_title","Name",350,true},
 Column{"rc_actions","Actions",130,true},Column{"rc_size","Size",90,true},Column{"rc_created","Published",140,true},
 Column{"rc_seeds","Seeds",65,true},Column{"rc_peers","Peers",65,true},Column{"rc_tags","Tags",150,true},
 Column{"rc_rating","Rating",70,true},Column{"rc_networks","Networks",100,false},Column{"rc_tracker","Tracker",160,false},
 Column{"rc_hash","Hash",280,false},Column{"rc_relto","Related To",240,false},Column{"rc_level","Level",60,false},
 Column{"rc_firstseen","First Seen",150,false},Column{"rc_lastseen","Last Seen",150,false},
 Column{"rc_changedlocallyon","Changed",150,false}};
QString date(qint64 secs){return secs>0?QDateTime::fromSecsSinceEpoch(secs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")):QString{};}
QString formattedSize(qint64 bytes){double v=double(bytes);const char* units[]={"B","KiB","MiB","GiB","TiB","PiB"};int u=0;while(v>=1024&&u<5){v/=1024;++u;}return QString::number(v,'f',u?1:0)+" "+QString::fromLatin1(units[u]);}
class Model final:public QAbstractTableModel {
public:
 std::shared_ptr<const rcm::Snapshot> snapshot;std::shared_ptr<const SwarmRatingValues> ratings;bool search=false;
 explicit Model(QObject* parent):QAbstractTableModel(parent){}
 int rowCount(const QModelIndex& parent={})const override{return parent.isValid()||!snapshot?0:int(snapshot->items.size());}
 int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(columns.size());}
 QVariant headerData(int section,Qt::Orientation orientation,int role)const override{if(orientation==Qt::Horizontal&&role==Qt::DisplayRole&&section>=0&&section<int(columns.size())){return tr(columns[section].title);}return {};}
 void reset(std::shared_ptr<const rcm::Snapshot> value,bool mode){beginResetModel();snapshot=std::move(value);search=mode;endResetModel();}
 void ratingValues(std::shared_ptr<const SwarmRatingValues> value){if(ratings==value){return;}ratings=std::move(value);if(rowCount()){emit dataChanged(index(0,9),index(rowCount()-1,9),{Qt::DisplayRole,Qt::UserRole,Qt::ToolTipRole});}}
 QVariant data(const QModelIndex& index,int role)const override{
  if(!index.isValid()||!snapshot||index.row()<0||std::size_t(index.row())>=snapshot->items.size()){return {};}const auto& item=snapshot->items[std::size_t(index.row())];const int column=index.column();if(role==Qt::UserRole+1){return column==0?QVariant(q(item.key)):QVariant{};}if(role==Qt::FontRole){if(item.unread){QFont font;font.setBold(true);return font;}return {};}
  if(role==Qt::UserRole){switch(column){case 0:return qint64(item.unread);case 1:return qint64(search?item.randoms.size()/4:item.randoms.size());case 4:return qint64(item.size);case 5:return qint64(item.publishedHours);case 6:return qint64(item.seeds());case 7:return qint64(item.leechers());case 14:return qint64(item.level);case 15:return qint64(item.firstSeen);case 16:return qint64(item.lastSeen);case 17:return qint64(item.changedOn);default:break;}}
  if(role!=Qt::DisplayRole&&role!=Qt::UserRole&&role!=Qt::ToolTipRole){return {};}QString text,tooltip;switch(column){case 0:text=item.unread?tr("New"):QString{};break;case 1:text=QString::number(search?item.randoms.size()/4:item.randoms.size());break;case 2:text=q(item.title);break;case 3:text=item.hash.size()==20?tr("Copy Magnet"):QString{};break;case 4:text=formattedSize(item.size);break;case 5:text=date(qint64(item.publishedHours)*3600);break;case 6:text=item.seeds()<0?QString{}:QString::number(item.seeds());break;case 7:text=item.leechers()<0?QString{}:QString::number(item.leechers());break;case 8:{QStringList tags;for(const auto& tag:item.tags){tags<<q(tag);}text=tags.join(", ");break;}case 9:{if(item.hash.size()==20&&(item.networks&1)&&item.tracker.empty()){const auto found=ratings?ratings->constFind(QByteArray::fromStdString(item.hash)):SwarmRatingValues::const_iterator{};if(ratings&&found!=ratings->cend()){text=found->text;tooltip=found->tooltip;}else{text=tr("Lookup");tooltip=tr("Look up Public ratings for this torrent");}}break;}case 10:{QStringList nets;if(item.networks&1){nets<<"Public";}if(item.networks&2){nets<<"I2P";}if(item.networks&4){nets<<"Tor";}text=nets.join(", ");break;}case 11:text=q(item.tracker);break;case 12:text=q(rcm::base32(item.hash));break;case 13:{QStringList parents;for(const auto& hash:item.relatedTo){parents<<q(rcm::base32(hash));}text=parents.join(", ");break;}case 14:text=QString::number(item.level);break;case 15:text=date(item.firstSeen);break;case 16:text=date(item.lastSeen);break;case 17:text=date(item.changedOn/1000);break;default:break;}
  if(role==Qt::ToolTipRole){const auto value=tooltip.isEmpty()?text:tooltip;return QStringLiteral("<qt>")+value.toHtmlEscaped().replace(QStringLiteral("\r\n"),QStringLiteral("<br>")).replace(QLatin1Char('\n'),QStringLiteral("<br>"))+QStringLiteral("</qt>");}return text;
 }
};
class Filter final:public QSortFilterProxyModel {
public:
 std::shared_ptr<const rcm::Snapshot> snapshot;QString text;bool unread=false,all=false,search=false;QByteArray parentHash;
 void refresh(){beginFilterChange();endFilterChange(Direction::Rows);}
 bool filterAcceptsRow(int row,const QModelIndex&)const override{
  if(!snapshot||row<0||std::size_t(row)>=snapshot->items.size()){return false;}const auto& item=snapshot->items[std::size_t(row)];
  if(!search&&unread&&!item.unread){return false;}if(!search&&!all&&!rcm::sourceVisible(item,snapshot->settings)){return false;}
  if(!search&&snapshot->settings.activeOnly&&item.seeds()<=0&&item.leechers()<=0){return false;}
  if(!search&&!parentHash.isEmpty()&&std::ranges::none_of(item.relatedTo,[&](const auto& hash){return QByteArray(hash.data(),qsizetype(hash.size()))==parentHash;})){return false;}
  if(!text.isEmpty()){QStringList tags;for(const auto& tag:item.tags){tags<<q(tag);}return (q(item.title)+" "+tags.join(' ')+" "+q(item.tracker)+" "+q(rcm::base32(item.hash))).contains(text,Qt::CaseInsensitive);}return true;
 }
};
const core::Node* at(const core::Node* v,std::size_t i){return v&&v->type==core::Node::Type::List&&v->children.size()>i?&v->children[i]:nullptr;}
qint64 integer(const core::Node* v,qint64 fallback){return v&&v->type==core::Node::Type::Integer?v->integer:fallback;}
}
SwarmDiscoveriesPage::SwarmDiscoveriesPage(QWidget* parent):QWidget(parent){
 setObjectName(QStringLiteral("swarm-discoveries-page"));auto* layout=new QVBoxLayout(this);
 status_=new QLabel(tr("Open a profile to use Swarm Discoveries"),this);status_->setObjectName("swarm-status");status_->setWordWrap(true);layout->addWidget(status_);
 auto* buttons=new QHBoxLayout;discover_=new QPushButton(tr("Discover now"),this);discover_->setObjectName("swarm-discover-now");buttons->addWidget(discover_);
 auto* refresh=new QPushButton(tr("Refresh"),this);buttons->addWidget(refresh);settings_=new QPushButton(tr("Settings…"),this);settings_->setObjectName("swarm-settings");buttons->addWidget(settings_);buttons->addStretch();count_=new QLabel(this);buttons->addWidget(count_);layout->addLayout(buttons);
 connect(discover_,&QPushButton::clicked,this,&SwarmDiscoveriesPage::discoverRequested);connect(refresh,&QPushButton::clicked,this,&SwarmDiscoveriesPage::refreshRequested);connect(settings_,&QPushButton::clicked,this,&SwarmDiscoveriesPage::configure);
 auto* searchRow=new QHBoxLayout;searchTerm_=new QLineEdit(this);searchTerm_->setObjectName("swarm-search-term");searchTerm_->setMaxLength(200);searchTerm_->setPlaceholderText(tr("Search Public DHT: words, tag: or hash:"));searchRow->addWidget(searchTerm_,1);searchButton_=new QPushButton(tr("Search"),this);searchButton_->setObjectName("swarm-search");searchRow->addWidget(searchButton_);cancelSearch_=new QPushButton(tr("Cancel search"),this);cancelSearch_->setObjectName("swarm-cancel-search");cancelSearch_->setEnabled(false);searchRow->addWidget(cancelSearch_);layout->addLayout(searchRow);searchStatus_=new QLabel(this);searchStatus_->setObjectName("swarm-search-status");searchStatus_->setWordWrap(true);layout->addWidget(searchStatus_);
 auto search=[this]{if(searchButton_->isEnabled()&&!searchTerm_->text().trimmed().isEmpty()){searchMode_=true;related_.clear();emit searchRequested(searchTerm_->text());}};connect(searchButton_,&QPushButton::clicked,this,search);connect(searchTerm_,&QLineEdit::returnPressed,this,search);connect(cancelSearch_,&QPushButton::clicked,this,&SwarmDiscoveriesPage::cancelSearchRequested);
 auto* filters=new QHBoxLayout;filter_=new QLineEdit(this);filter_->setObjectName("swarm-filter");filter_->setPlaceholderText(tr("Filter names, tags or hashes"));filter_->setClearButtonEnabled(true);filters->addWidget(filter_);
 unread_=new QCheckBox(tr("Unread only"),this);unread_->setObjectName("swarm-unread-only");filters->addWidget(unread_);allSources_=new QCheckBox(tr("Show all sources"),this);allSources_->setToolTip(tr("Include results outside the configured trusted sources."));filters->addWidget(allSources_);
 auto* clearParent=new QPushButton(tr("All discoveries"),this);connect(clearParent,&QPushButton::clicked,this,[this]{searchMode_=false;display_=value_.profile;rebuild();filterRelated({});});filters->addWidget(clearParent);layout->addLayout(filters);
 connect(filter_,&QLineEdit::textChanged,this,&SwarmDiscoveriesPage::updateFilter);connect(unread_,&QCheckBox::toggled,this,&SwarmDiscoveriesPage::updateFilter);connect(allSources_,&QCheckBox::toggled,this,&SwarmDiscoveriesPage::updateFilter);
 model_=new Model(this);
 auto* proxy=new Filter;proxy_=proxy;proxy_->setParent(this);proxy_->setSourceModel(model_);proxy_->setSortRole(Qt::UserRole);
 table_=new QTableView(this);table_->setObjectName("swarm-table");table_->setHorizontalHeader(new ClassicSortHeader(table_));table_->setModel(proxy_);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::ExtendedSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSortingEnabled(true);table_->setAlternatingRowColors(true);table_->verticalHeader()->hide();table_->horizontalHeader()->setSectionsMovable(true);table_->horizontalHeader()->setSortIndicator(1,Qt::DescendingOrder);layout->addWidget(table_,1);
 connect(table_,&QTableView::clicked,this,[this](const QModelIndex& index){if(index.column()==3){copyMagnet();}else if(index.column()==9){lookupRating(index);}});
 table_->setContextMenuPolicy(Qt::CustomContextMenu);connect(table_,&QTableView::customContextMenuRequested,this,&SwarmDiscoveriesPage::contextMenu);connect(table_,&QTableView::doubleClicked,this,[this]{markSelected(false);});
 layoutTimer_=new QTimer(this);layoutTimer_->setSingleShot(true);layoutTimer_->setInterval(500);connect(layoutTimer_,&QTimer::timeout,this,&SwarmDiscoveriesPage::saveLayout);
 auto changed=[this]{if(!applying_&&layoutApplied_){layoutTimer_->start();}};connect(table_->horizontalHeader(),&QHeaderView::sectionMoved,this,changed);connect(table_->horizontalHeader(),&QHeaderView::sectionResized,this,changed);connect(table_->horizontalHeader(),&QHeaderView::sortIndicatorChanged,this,changed);
 auto* header=table_->horizontalHeader();header->setContextMenuPolicy(Qt::CustomContextMenu);connect(header,&QHeaderView::customContextMenuRequested,this,[this,header](QPoint point){QMenu menu(this);for(int c=0;c<int(columns.size());++c){auto* a=menu.addAction(tr(columns[c].title));a->setCheckable(true);a->setChecked(!table_->isColumnHidden(c));connect(a,&QAction::toggled,this,[this,c](bool on){table_->setColumnHidden(c,!on);layoutTimer_->start();});}menu.exec(header->mapToGlobal(point));});
 searchButton_->setEnabled(false);discover_->setEnabled(false);settings_->setEnabled(false);applyLayout();
}
void SwarmDiscoveriesPage::setSnapshot(SwarmViewSnapshot value){
 if(value.generation<value_.generation||(value.generation==value_.generation&&value.sequence<value_.sequence)){return;}
 const bool generation=value.generation!=value_.generation;const bool changed=value.profile!=value_.profile||value.search!=value_.search;const bool ratingsChanged=value.ratings!=value_.ratings;
 if(generation){layoutApplied_=false;related_.clear();layoutTimer_->stop();searchMode_=false;lastSearchId_=0;}value_=std::move(value);
 if(value_.search&&value_.search->id!=lastSearchId_){lastSearchId_=value_.search->id;searchMode_=true;related_.clear();}
 if(changed||generation||!display_){display_=value_.profile;if(searchMode_&&value_.search){auto results=std::make_shared<rcm::Snapshot>();if(value_.profile){results->settings=value_.profile->settings;results->tableLayout=value_.profile->tableLayout;}results->writable=false;results->items=value_.search->results;display_=std::move(results);}}
 searchButton_->setEnabled(value_.profile&&value_.profile->settings.enabled);cancelSearch_->setEnabled(value_.search&&value_.search->running);searchStatus_->setText(value_.search?tr("%1 · %2 results · %3 contacts · %4 active").arg(q(value_.search->status)).arg(value_.search->results.size()).arg(value_.search->contacts).arg(value_.search->active):QString{});
 unread_->setEnabled(!searchMode_);allSources_->setEnabled(!searchMode_);
 const auto& p=value_.profile;discover_->setEnabled(p&&p->writable&&p->settings.enabled);settings_->setEnabled(p&&p->writable);
 status_->setText(tr("%1 · lookups: %2 · publications: %3 · active: %4 · queued: %5").arg(value_.status).arg(value_.lookups).arg(value_.publications).arg(value_.active).arg(value_.pending));
 if(changed){rebuild();if(!layoutApplied_&&p){applyLayout();layoutApplied_=true;}}
 if(ratingsChanged||changed||generation){updateRatingCells();}
}
void SwarmDiscoveriesPage::rebuild(){
 const auto selected=selectedKeys();const int horizontal=table_->horizontalScrollBar()->value(),vertical=table_->verticalScrollBar()->value();auto* filter=static_cast<Filter*>(proxy_);filter->snapshot=display_;filter->search=searchMode_;static_cast<Model*>(model_)->reset(display_&&(searchMode_||display_->settings.uiEnabled)?display_:nullptr,searchMode_);updateFilter();
 if(!selected.empty()){for(int row=0;row<proxy_->rowCount();++row){const auto index=proxy_->index(row,0);if(selected.contains(index.data(Qt::UserRole+1).toString())){table_->selectionModel()->select(index,QItemSelectionModel::Select|QItemSelectionModel::Rows);}}}table_->horizontalScrollBar()->setValue(horizontal);table_->verticalScrollBar()->setValue(vertical);updateRatingCells();
}
void SwarmDiscoveriesPage::updateFilter(){auto* f=static_cast<Filter*>(proxy_);f->text=filter_->text();f->unread=unread_->isChecked();f->all=allSources_->isChecked();f->parentHash=related_;f->refresh();count_->setText(searchMode_?tr("%1 shown / %2 search results").arg(proxy_->rowCount()).arg(display_?display_->items.size():0):tr("%1 shown / %2 saved · %3 unread%4").arg(proxy_->rowCount()).arg(value_.profile?value_.profile->items.size():0).arg(value_.profile?value_.profile->unread:0).arg(related_.isEmpty()?QString{}:tr(" · related filter")));}
void SwarmDiscoveriesPage::lookupRating(const QModelIndex& index){
 if(!index.isValid()||index.column()!=9||!display_){return;}const auto source=proxy_->mapToSource(index);if(source.row()<0||std::size_t(source.row())>=display_->items.size()){return;}const auto& item=display_->items[std::size_t(source.row())];if(item.hash.size()!=20||!(item.networks&1)||!item.tracker.empty()){return;}const QByteArray hash(item.hash.data(),qsizetype(item.hash.size()));if(value_.ratings&&value_.ratings->contains(hash)){return;}emit ratingRequested(value_.generation,hash);
}
void SwarmDiscoveriesPage::updateRatingCells(){const int horizontal=table_->horizontalScrollBar()->value(),vertical=table_->verticalScrollBar()->value();static_cast<Model*>(model_)->ratingValues(value_.ratings);table_->horizontalScrollBar()->setValue(horizontal);table_->verticalScrollBar()->setValue(vertical);}
void SwarmDiscoveriesPage::filterRelated(QByteArray hash){if(searchMode_&&!hash.isEmpty()){searchMode_=false;display_=value_.profile;rebuild();}related_=std::move(hash);updateFilter();}
QStringList SwarmDiscoveriesPage::selectedKeys()const{QStringList out;for(const auto& i:table_->selectionModel()->selectedRows()){out<<i.data(Qt::UserRole+1).toString();}return out;}
const rcm::Item* SwarmDiscoveriesPage::selectedItem()const{const auto rows=table_->selectionModel()->selectedRows();if(rows.size()!=1||!display_){return nullptr;}const auto row=proxy_->mapToSource(rows.front()).row();return row>=0&&std::size_t(row)<display_->items.size()?&display_->items[std::size_t(row)]:nullptr;}
void SwarmDiscoveriesPage::markSelected(bool unread){if(searchMode_){return;}const auto keys=selectedKeys();if(!keys.isEmpty()){emit readRequested(keys,unread);}}
void SwarmDiscoveriesPage::copyMagnet(){if(const auto* item=selectedItem();item&&item->hash.size()==20&&item->tracker.empty()){QApplication::clipboard()->setText(QStringLiteral("magnet:?xt=urn:btih:")+q(rcm::base32(item->hash))+QStringLiteral("&dn=")+QString::fromLatin1(QUrl::toPercentEncoding(q(item->title))));}}
void SwarmDiscoveriesPage::contextMenu(QPoint point){QMenu menu(this);const auto* item=selectedItem();auto* related=menu.addAction(tr("Discover related torrents"));related->setEnabled(!searchMode_&&item&&item->hash.size()==20&&item->tracker.empty());connect(related,&QAction::triggered,this,[this]{if(const auto* row=selectedItem()){const QByteArray hash(row->hash.data(),qsizetype(row->hash.size()));filterRelated(hash);emit lookupRequested(hash);}});auto* magnet=menu.addAction(tr("Copy Magnet"));magnet->setEnabled(item&&item->hash.size()==20&&item->tracker.empty());connect(magnet,&QAction::triggered,this,&SwarmDiscoveriesPage::copyMagnet);menu.addSeparator();for(bool unread:{false,true}){auto* a=menu.addAction(unread?tr("Mark unread"):tr("Mark read"));a->setEnabled(!searchMode_&&!selectedKeys().isEmpty());connect(a,&QAction::triggered,this,[this,unread]{markSelected(unread);});}auto* remove=menu.addAction(tr("Remove from discoveries"));remove->setEnabled(!searchMode_&&!selectedKeys().isEmpty());connect(remove,&QAction::triggered,this,[this]{emit removeRequested(selectedKeys());});menu.exec(table_->viewport()->mapToGlobal(point));}
void SwarmDiscoveriesPage::configure(){
 if(!value_.profile){return;}const auto initial=value_.profile->settings;QDialog dialog(this);dialog.setWindowTitle(tr("Swarm Discoveries Settings"));auto* layout=new QFormLayout(&dialog);
 auto check=[&](const char* title,bool value){auto* c=new QCheckBox(tr(title),&dialog);c->setChecked(value);layout->addRow(c);return c;};
 auto* enabled=check("Enable discoveries on the Public network",initial.enabled);auto* ui=check("Show saved discoveries",initial.uiEnabled);auto* active=check("Show active swarms only",initial.activeOnly);auto* bigly=check("Use the BiglyBT DHT for lookups",initial.useBigly);auto* names=check("Publish original torrent names",initial.useTorrentNames);
 auto* max=new QSpinBox(&dialog);max->setObjectName("swarm-maximum");max->setRange(0,std::numeric_limits<int>::max());max->setValue(initial.maximum);layout->addRow(tr("Maximum saved discoveries"),max);auto* depth=new QSpinBox(&dialog);depth->setRange(1,10);depth->setValue(initial.depth);layout->addRow(tr("Maximum search depth"),depth);
 auto* sources=new QPlainTextEdit(&dialog);QStringList values;for(const auto& s:initial.sources){values<<q(s);}sources->setPlainText(values.join('\n'));layout->addRow(tr("Trusted sources (one domain per line; * for all)"),sources);
 auto* reset=new QPushButton(tr("Allow previously removed discoveries again"),&dialog);layout->addRow(reset);connect(reset,&QPushButton::clicked,this,[this,reset]{emit resetDeletedRequested();reset->setEnabled(false);});
 auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 if(dialog.exec()!=QDialog::Accepted){return;}auto s=initial;s.maximum=max->value();s.depth=depth->value();s.enabled=enabled->isChecked();s.uiEnabled=ui->isChecked();s.activeOnly=active->isChecked();s.useBigly=bigly->isChecked();s.useTorrentNames=names->isChecked();s.sources.clear();for(const auto& line:sources->toPlainText().split('\n',Qt::SkipEmptyParts)){const auto source=line.trimmed().toLower();if(!source.isEmpty()){s.sources.push_back(source.toStdString());}}emit settingsRequested(std::move(s));
}
void SwarmDiscoveriesPage::applyLayout(){
 applying_=true;const auto doc=core::Document::parse(value_.profile?value_.profile->tableLayout:"de");const auto* root=doc?&doc->root():nullptr;auto* header=table_->horizontalHeader();std::vector<std::pair<qint64,int>> order;int sort=1;Qt::SortOrder direction=Qt::DescendingOrder;
 std::string sortId;if(root){if(const auto* sorts=root->find("SortColumns");sorts&&sorts->type==core::Node::Type::List&&!sorts->children.empty()&&sorts->children.front().type==core::Node::Type::Bytes){sortId=sorts->children.front().bytes;}else if(const auto* s=root->find("SortColumn");s&&s->type==core::Node::Type::Bytes){sortId=s->bytes;}}
 for(int c=0;c<int(columns.size());++c){const auto* saved=root?root->find(std::string("Column.")+columns[c].id):nullptr;const auto width=integer(at(saved,2),columns[c].width);table_->setColumnWidth(c,int(std::clamp<qint64>(width,24,4096)));table_->setColumnHidden(c,integer(at(saved,0),columns[c].visible?1:0)!=1);const auto* position=at(saved,1);order.emplace_back(position&&position->type==core::Node::Type::Integer&&position->integer>=0?position->integer:1000000+c,c);if(sortId==columns[c].id){sort=c;direction=integer(at(saved,4),c==1?0:1)==1?Qt::AscendingOrder:Qt::DescendingOrder;}}
 std::stable_sort(order.begin(),order.end());for(int pos=0;pos<int(order.size());++pos){header->moveSection(header->visualIndex(order[pos].second),pos);}table_->sortByColumn(sort,direction);applying_=false;
}
void SwarmDiscoveriesPage::saveLayout(){
 if(applying_||!value_.profile){return;}auto doc=core::Document::parse(value_.profile->tableLayout);if(!doc){return;}auto* header=table_->horizontalHeader();
 for(int c=0;c<int(columns.size());++c){const std::string key=std::string("Column.")+columns[c].id;const auto* saved=doc->find(key);std::string list="l";for(std::size_t i=0;i<5;++i){qint64 value=0;switch(i){case 0:value=table_->isColumnHidden(c)?0:1;break;case 1:value=header->visualIndex(c);break;case 2:value=table_->columnWidth(c);if(value==0){value=integer(at(saved,2),columns[c].width);}break;case 3:value=integer(at(saved,3),1);break;case 4:value=header->sortIndicatorSection()==c?(header->sortIndicatorOrder()==Qt::AscendingOrder?1:0):integer(at(saved,4),-1);break;}list+=n(value);}if(saved&&saved->type==core::Node::Type::List){for(std::size_t i=5;i<saved->children.size();++i){auto encoded=core::encodeValue(saved->children[i]);if(encoded){list+=*encoded;}}}list+='e';if(!doc->replaceValue(key,std::move(list))){return;}}
 const int sort=header->sortIndicatorSection();if(sort>=0&&sort<int(columns.size())){if(!doc->replaceValue("SortColumns","l"+b(columns[sort].id)+"e")){return;}}const auto raw=doc->serialize();emit layoutRequested(QByteArray(raw.data(),qsizetype(raw.size())));
}
}
