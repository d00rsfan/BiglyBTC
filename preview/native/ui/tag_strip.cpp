// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_strip.hpp"
#include <QApplication>
#include <QActionGroup>
#include <QFrame>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QImageReader>
#include <QFileInfo>
#include <QUrl>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QTableView>
#include <QTimer>
#include <functional>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace biglybtc::ui {
namespace {
constexpr auto DownloadMime="application/x-biglybtc-downloads";
class ChipButton final:public QToolButton {
 QTimer timer_;bool held_=false;
 bool canDrop(const QDropEvent* e)const{auto* source=qobject_cast<QTableView*>(e->source());return isEnabled()&&(!mayAssign||mayAssign())&&source&&source->window()==window()&&e->mimeData()->hasFormat(DownloadMime)&&e->mimeData()->data(DownloadMime).size()<=7*1024*1024;}
 void mousePressEvent(QMouseEvent* e)override{held_=false;QToolButton::mousePressEvent(e);if(e->button()==Qt::LeftButton){timer_.start();}}
 void mouseMoveEvent(QMouseEvent* e)override{if(!rect().contains(e->position().toPoint())){timer_.stop();}QToolButton::mouseMoveEvent(e);}
 void mouseReleaseEvent(QMouseEvent* e)override{timer_.stop();if(held_){held_=false;setDown(false);e->accept();return;}QToolButton::mouseReleaseEvent(e);}
 void dragEnterEvent(QDragEnterEvent* e)override{if(canDrop(e)){e->acceptProposedAction();}else{e->ignore();}}
 void dragMoveEvent(QDragMoveEvent* e)override{if(canDrop(e)){e->acceptProposedAction();}else{e->ignore();}}
 void dropEvent(QDropEvent* e)override{if(canDrop(e)&&dropped){auto callback=dropped;e->acceptProposedAction();callback(e->source(),e->mimeData()->data(DownloadMime));}else{e->ignore();}}
public:
 std::function<bool()> mayAssign;std::function<void()> held;std::function<void(QObject*,QByteArray)> dropped;
 explicit ChipButton(QWidget* parent):QToolButton(parent){setAcceptDrops(true);timer_.setSingleShot(true);timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{if(isDown()&&held&&(!mayAssign||mayAssign())){held_=true;setDown(false);auto callback=held;callback();}});}
};
class TableDrag final:public QObject {
 QPointer<QTableView> table_;QPoint start_;
 bool eventFilter(QObject* object,QEvent* event)override{
  if(!table_||object!=table_->viewport()){return false;}if(event->type()==QEvent::MouseButtonPress){start_=static_cast<QMouseEvent*>(event)->position().toPoint();return false;}
  if(event->type()!=QEvent::MouseMove){return false;}const auto* mouse=static_cast<QMouseEvent*>(event);if(!mouse->buttons().testFlag(Qt::LeftButton)||(mouse->position().toPoint()-start_).manhattanLength()<QApplication::startDragDistance()){return false;}
  const auto rows=table_->selectionModel()->selectedRows();if(rows.empty()||rows.size()>qsizetype(LibraryModel::MaximumRows)){return false;}QByteArray hashes="session:"+QByteArray::number(table_->property("BiglyBTC.librarySession").toULongLong())+'\n';for(const auto& row:rows){const auto hash=row.data(LibraryModel::HashRole).toString().toLatin1();if(hash.size()!=40&&hash.size()!=64){return false;}hashes+=hash;hashes+='\n';}auto* mime=new QMimeData;mime->setData(DownloadMime,hashes);QPointer<QDrag> drag=new QDrag(table_);drag->setMimeData(mime);drag->exec(Qt::CopyAction|Qt::MoveAction,Qt::CopyAction);if(drag){drag->deleteLater();}return true;
 }
public:
 explicit TableDrag(QTableView* table):QObject(table),table_(table){table->viewport()->installEventFilter(this);}
};
class ChipFlow final:public QLayout {
 QList<QLayoutItem*> items_;Qt::Alignment alignment_=Qt::AlignHCenter;
 int arrange(const QRect& rect,bool apply)const{
  const auto margin=contentsMargins();const auto area=rect.marginsRemoved(margin);int y=area.y(),height=0;qsizetype start=0;
  while(start<items_.size()){qsizetype end=start;int used=0,rowHeight=0;for(;end<items_.size();++end){const auto size=items_[end]->sizeHint();const auto width=std::min(size.width(),std::max(1,area.width()));if(end>start&&used+spacing()+width>area.width()){break;}used+=(end>start?spacing():0)+width;rowHeight=std::max(rowHeight,size.height());}
   int x=area.x();if(alignment_==Qt::AlignHCenter){x+=std::max(0,(area.width()-used)/2);}else if(alignment_==Qt::AlignRight){x+=std::max(0,area.width()-used);}for(auto i=start;i<end;++i){const auto size=items_[i]->sizeHint();const auto width=std::min(size.width(),std::max(1,area.width()));if(apply){items_[i]->setGeometry(QRect(x,y,width,rowHeight));}x+=width+spacing();}y+=rowHeight+spacing();height+=rowHeight+(start?spacing():0);start=end;
  }return height+margin.top()+margin.bottom();
 }
public:
 explicit ChipFlow(QWidget* parent):QLayout(parent){setContentsMargins(0,0,0,0);setSpacing(4);}
 ~ChipFlow()override{while(auto* item=takeAt(0)){delete item;}}
 void setAlignmentMode(int value){const auto next=value==1?Qt::AlignLeft:value==3||value<0||value>3?Qt::AlignRight:Qt::AlignHCenter;if(alignment_!=next){alignment_=next;invalidate();}}
 void addItem(QLayoutItem* item)override{items_.append(item);}int count()const override{return int(items_.size());}QLayoutItem* itemAt(int n)const override{return items_.value(n);}QLayoutItem* takeAt(int n)override{return n>=0&&n<items_.size()?items_.takeAt(n):nullptr;}
 QSize sizeHint()const override{return {300,heightForWidth(300)};}QSize minimumSize()const override{return {0,24};}Qt::Orientations expandingDirections()const override{return Qt::Horizontal;}bool hasHeightForWidth()const override{return true;}int heightForWidth(int width)const override{return arrange(QRect(0,0,width,0),false);}void setGeometry(const QRect& rect)override{QLayout::setGeometry(rect);arrange(rect,true);}
};
bool sameLabels(const TagStripPresentation& a,const TagStripPresentation& b){if(!a||!b){return a==b;}if(a->chips.size()!=b->chips.size()){return false;}for(qsizetype i=0;i<a->chips.size();++i){const auto& x=a->chips[i];const auto& y=b->chips[i];if(x.key!=y.key||x.name!=y.name||x.group!=y.group||x.colour!=y.colour||x.imageFile!=y.imageFile||x.filter!=y.filter||x.kind!=y.kind){return false;}}return true;}
}
void installTagDragSource(QTableView* table){if(table){new TableDrag(table);}}
TagStrip::TagStrip(bool completed,QWidget* parent):QWidget(parent),completed_(completed),scroll_(new QScrollArea(this)),content_(new QWidget){setObjectName(completed?"seed-tag-strip":"leech-tag-strip");new ChipFlow(content_);scroll_->setWidget(content_);scroll_->setWidgetResizable(true);scroll_->setFrameShape(QFrame::NoFrame);scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setMinimumWidth(0);setFixedHeight(26);scroll_->viewport()->setAutoFillBackground(false);content_->setAutoFillBackground(false);setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint p){contextMenu(mapToGlobal(p));});content_->setContextMenuPolicy(Qt::CustomContextMenu);connect(content_,&QWidget::customContextMenuRequested,this,[this](QPoint p){contextMenu(content_->mapToGlobal(p));});hide();}
void TagStrip::beginSession(){++binding_;selection_=TagStripSelection{};data_.reset();settings_={};failure_.clear();writable_=false;rebuild();}
void TagStrip::setSnapshot(TagStripPresentation data,TagStripSettings settings,QString failure){if(data_==data&&settings_==settings&&failure_==failure){return;}if(data_&&data&&data_->session!=data->session){beginSession();}const bool labels=sameLabels(data_,data);const bool display=settings_.categories!=settings.categories||settings_.tags!=settings.tags||settings_.categoriesCompletedOnly!=settings.categoriesCompletedOnly||settings_.tagsCompletedOnly!=settings.tagsCompletedOnly||settings_.filtersOnly!=settings.filtersOnly||settings_.imageOverride!=settings.imageOverride;data_=std::move(data);settings_=settings;failure_=std::move(failure);selection_.setData(data_);selection_.setInclusive(settings.inclusive);static_cast<ChipFlow*>(content_->layout())->setAlignmentMode(settings.alignment);if(!labels||display){rebuild();}else{updateButtons();}setToolTip(failure_.toHtmlEscaped());}
void TagStrip::rebuild(){auto* flow=static_cast<ChipFlow*>(content_->layout());while(auto* item=flow->takeAt(0)){delete item->widget();delete item;}visibleKeys_.clear();std::optional<QString> group;bool priorManual=false,haveCategory=false;
 if(data_){for(const auto& chip:data_->chips){if(!tagChipShown(chip,settings_,completed_)){continue;}const bool manual=chip.kind==TagChipKind::Manual;if(manual&&((!priorManual&&haveCategory)||(priorManual&&group&&group!=chip.group))){auto* line=new QFrame(content_);line->setFrameShape(QFrame::VLine);line->setFixedSize(8,20);flow->addWidget(line);}auto* button=new ChipButton(content_);button->setObjectName("classic-tag-chip");const auto binding=binding_;button->mayAssign=[this,binding]{return binding==binding_&&writable_;};button->held=[this,key=chip.key,binding]{if(binding==binding_&&writable_){emit assignmentRequested(key);}};button->dropped=[this,key=chip.key,binding](QObject* source,QByteArray hashes){if(binding==binding_&&writable_){emit assignmentDropped(key,source,std::move(hashes));}};button->setProperty("chipKey",chip.key);button->setCheckable(true);button->setText(QString(chip.name).replace('&',"&&"));button->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);if(chip.colour){const auto& rgb=*chip.colour;QPixmap icon(10,10);icon.fill(Qt::transparent);QPainter painter(&icon);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor(rgb[0],rgb[1],rgb[2]));painter.drawEllipse(1,1,8,8);painter.end();button->setIcon(QIcon(icon));}button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setAccessibleName(chip.name);if(settings_.imageOverride&&!chip.imageFile.isEmpty()){const QUrl url(chip.imageFile);const auto file=url.isLocalFile()?url.toLocalFile():chip.imageFile;if(QFileInfo(file).size()<=4*1024*1024){QImageReader reader(file);reader.setScaledSize(QSize(20,20));const auto image=reader.read();if(!image.isNull()){button->setIcon(QIcon(QPixmap::fromImage(image)));button->setText(QString{});button->setToolButtonStyle(Qt::ToolButtonIconOnly);}}}connect(button,&QToolButton::clicked,this,[this,key=chip.key]{selection_.click(key,QApplication::keyboardModifiers().testFlag(Qt::ControlModifier));updateButtons();emit selectionChanged();});button->setContextMenuPolicy(Qt::CustomContextMenu);connect(button,&QWidget::customContextMenuRequested,this,[this,button,key=chip.key](QPoint p){contextMenu(button->mapToGlobal(p),key);});flow->addWidget(button);visibleKeys_.push_back(chip.key);priorManual=manual;haveCategory|=!manual;group=chip.group;}}
 flow->invalidate();updateButtons();setVisible(!visibleKeys_.isEmpty());desiredHeight_=std::clamp(flow->heightForWidth(std::max(1,width())),24,116);setFixedHeight(desiredHeight_);updateGeometry();}
void TagStrip::updateButtons(){for(auto* button:content_->findChildren<QToolButton*>(QString(),Qt::FindDirectChildrenOnly)){const auto key=button->property("chipKey").toString();button->setChecked(selection_.selected().contains(key));if(data_){const auto found=data_->byKey.constFind(key);if(found!=data_->byKey.cend()){const auto& chip=data_->chips[*found];button->setToolTip(chip.name.toHtmlEscaped()+(chip.kind==TagChipKind::Manual?tr(" — %1 torrents").arg(chip.hashes.size()):QString{}));}}}}
void TagStrip::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);scroll_->setGeometry(rect());const auto height=std::clamp(content_->layout()->heightForWidth(std::max(1,e->size().width()-4)),24,116);if(desiredHeight_!=height){desiredHeight_=height;setFixedHeight(height);updateGeometry();}}
QSize TagStrip::sizeHint()const{return {200,desiredHeight_};}QSize TagStrip::minimumSizeHint()const{return {0,24};}
void TagStrip::contextMenu(QPoint global,QString chip){auto* menu=new QMenu(this);menu->setObjectName("classic-tag-strip-menu");const auto expected=settings_;const auto binding=binding_;if(!chip.isEmpty()&&data_&&data_->byKey.contains(chip)){auto* assign=menu->addAction(tr("Assign to selected torrents"));assign->setObjectName("classic-tag-chip-assign");assign->setEnabled(writable_);connect(assign,&QAction::triggered,this,[this,binding,chip]{if(binding==binding_&&writable_){emit assignmentRequested(chip);}});menu->addSeparator();}const auto add=[&](QString label,auto member){auto* action=menu->addAction(label);action->setCheckable(true);action->setChecked(expected.*member);action->setEnabled(writable_);connect(action,&QAction::triggered,this,[this,expected,member,binding](bool value){if(binding_==binding&&writable_&&settings_==expected){auto next=expected;next.*member=value;emit settingsRequested(expected,next);}});};add(tr("Show Categories"),&TagStripSettings::categories);add(tr("Show Tags"),&TagStripSettings::tags);menu->addSeparator();add(tr("Categories only in Seeding"),&TagStripSettings::categoriesCompletedOnly);add(tr("Tags only in Seeding"),&TagStripSettings::tagsCompletedOnly);add(tr("Show filter tags only"),&TagStripSettings::filtersOnly);add(tr("Images instead of tag names"),&TagStripSettings::imageOverride);add(tr("Match any selected category or tag"),&TagStripSettings::inclusive);auto* align=menu->addMenu(tr("Alignment"));auto* group=new QActionGroup(align);for(int value=0;value<4;++value){const QStringList names{tr("Automatic"),tr("Left"),tr("Centre"),tr("Right")};auto* action=align->addAction(names[value]);action->setCheckable(true);action->setChecked(expected.alignment==value);action->setEnabled(writable_);group->addAction(action);connect(action,&QAction::triggered,this,[this,expected,value,binding]{if(binding_==binding&&writable_&&settings_==expected){auto next=expected;next.alignment=value;emit settingsRequested(expected,next);}});}menu->addSeparator();connect(menu->addAction(tr("Categories…")),&QAction::triggered,this,[this,binding]{if(binding_==binding){emit categoriesRequested();}});connect(menu->addAction(tr("Tags…")),&QAction::triggered,this,[this,binding]{if(binding_==binding){emit tagsRequested();}});connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);menu->popup(global);}
}
