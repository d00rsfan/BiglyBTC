#include "tracker_statistics_view.hpp"
#include <QAbstractTableModel>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <array>
#include <functional>
#include <limits>

namespace biglybtc::ui {
namespace {
QVariant number(const std::optional<int>& n){return n?QVariant::fromValue(*n):QVariant{};}
QVariant flag(const std::optional<bool>& n){return n?QVariant{*n?1:0}:QVariant{};}
class Model final:public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  int rowCount(const QModelIndex& p={})const override{return p.isValid()||!packet_||!packet_->snapshot?0:static_cast<int>(packet_->snapshot->rows.size());}
  int columnCount(const QModelIndex& p={})const override{return p.isValid()?0:13;}
  QVariant headerData(int section,Qt::Orientation orientation,int role)const override{
    if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole){return {};}
    const std::array<const char*,13> names{"Tracker origin","Tier","Hash","Local socket","Verified","Enabled","Updating","Failures","Seeds","Leechers","Downloaded","Next announce (s)","Error code"};
    return section>=0&&section<static_cast<int>(names.size())?tr(names[static_cast<std::size_t>(section)]):QVariant{};
  }
  QVariant data(const QModelIndex& i,int role)const override{
    if(!i.isValid()||i.model()!=this||i.row()<0||i.row()>=rowCount()||i.column()<0||i.column()>=13){return {};}
    const auto& r=packet_->snapshot->rows[static_cast<std::size_t>(i.row())];QVariant n;
    switch(i.column()){
      case 0:if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole){return QString::fromStdString(r.displayUrl);}break;
      case 1:n=static_cast<int>(r.tier);break;
      case 2:if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole){return r.protocol?QStringLiteral("v%1").arg(r.protocol):tr("Unavailable");}n=r.protocol?QVariant{r.protocol}:QVariant{};break;
      case 3:if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole){return r.localEndpoint.empty()?tr("Unavailable"):QString::fromStdString(r.localEndpoint);}break;
      case 4:n=r.verified?1:0;break;case 5:n=flag(r.enabled);break;case 6:n=flag(r.updating);break;case 7:n=number(r.failures);break;
      case 8:n=number(r.seeders);break;case 9:n=number(r.leechers);break;case 10:n=number(r.downloaded);break;
      case 11:n=r.nextAnnounceSeconds?QVariant::fromValue(static_cast<qlonglong>(*r.nextAnnounceSeconds)):QVariant{};break;case 12:n=number(r.errorCode);break;
    }
    if(role==Qt::UserRole){return n;}
    if(role==Qt::ToolTipRole){return tr("Actual tracker snapshot. Scrape values may be unavailable. Next announce is a signed interval at observation time, not a live countdown or a guaranteed request deadline. URL credentials, paths and queries are hidden.");}
    if(role!=Qt::DisplayRole&&role!=Qt::AccessibleTextRole){return {};}
    if(i.column()>=4&&i.column()<=6){return !n.isValid()?tr("Unavailable"):n.toInt()?tr("Yes"):tr("No");}
    return n.isValid()?n.toString():tr("Unavailable");
  }
  bool replace(TrackerStatisticsPresentation p,const std::function<bool()>& current){
    QPointer<Model> self(this);beginResetModel();if(!self){return false;}const bool commit=current();if(!self){return false;}
    if(commit){packet_=std::move(p);}endResetModel();return commit&&self;
  }
private:TrackerStatisticsPresentation packet_;
};
class Proxy final:public QSortFilterProxyModel {
public:using QSortFilterProxyModel::QSortFilterProxyModel;
protected:bool lessThan(const QModelIndex& a,const QModelIndex& b)const override{
    if(a.column()!=0&&a.column()!=3){const auto x=a.data(Qt::UserRole),y=b.data(Qt::UserRole);
      if(x.isValid()!=y.isValid()){return x.isValid();}if(x.isValid()){return x.toLongLong()<y.toLongLong();}}
    return QString::compare(a.data().toString(),b.data().toString(),Qt::CaseSensitive)<0;
  }
};
}
TrackerStatisticsView::TrackerStatisticsView(QWidget* parent):QWidget(parent){
  setObjectName(QStringLiteral("tracker-statistics-view"));setAccessibleName(tr("Tracker announce observations"));auto* layout=new QVBoxLayout(this);
  status_=new QLabel(tr("Tracker observations unavailable"),this);status_->setObjectName(QStringLiteral("tracker-status"));status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
  filter_=new QLineEdit(this);filter_->setObjectName(QStringLiteral("tracker-filter"));filter_->setMaxLength(256);filter_->setPlaceholderText(tr("Filter tracker origins"));filter_->setAccessibleName(tr("Filter tracker origins"));layout->addWidget(filter_);
  model_=new Model(this);proxy_=new Proxy(this);proxy_->setSourceModel(model_);proxy_->setFilterKeyColumn(0);proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  table_=new QTableView(this);table_->setObjectName(QStringLiteral("tracker-table"));table_->setAccessibleName(tr("Actual tracker announce state"));table_->setModel(proxy_);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setWordWrap(false);table_->setSortingEnabled(true);table_->sortByColumn(0,Qt::AscendingOrder);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  constexpr std::array<int,13> widths{260,55,60,150,75,75,75,70,65,80,100,155,90};
  for(std::size_t i=0;i<widths.size();++i){table_->setColumnWidth(static_cast<int>(i),widths[i]);}layout->addWidget(table_,1);
  auto* scope=new QLabel(tr("Read-only tracker state from the observed torrent. Missing responses remain unavailable. Reading this page does not announce, scrape or enable trackers. No announce queue is inferred."),this);scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);layout->addWidget(scope);
  connect(filter_,&QLineEdit::textChanged,this,[this](const QString& value){if(revision_==std::numeric_limits<quint64>::max()){return;}++revision_;search_=value;dirty_=true;queueRefresh();});
}
bool TrackerStatisticsView::beginSession(quint64 session){if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}session_=session;++revision_;packet_.reset();dirty_=true;refresh();return true;}
bool TrackerStatisticsView::setPresentation(TrackerStatisticsPresentation p){if(revision_==std::numeric_limits<quint64>::max()||!acceptTrackerStatistics(packet_,p,session_)){return false;}++revision_;packet_=std::move(p);dirty_=true;refresh();return true;}
void TrackerStatisticsView::queueRefresh(){if(queued_){return;}queued_=true;QPointer<TrackerStatisticsView> self(this);QMetaObject::invokeMethod(this,[self]{if(self){self->queued_=false;self->refresh();}},Qt::QueuedConnection);}
void TrackerStatisticsView::refresh(){
  if(!dirty_||rendering_||!isVisible()||window()->isMinimized()||!status_||!filter_||!table_||!model_||!proxy_||table_->model()!=proxy_||proxy_->sourceModel()!=model_){return;}
  const auto packet=packet_;const auto revision=revision_;QPointer<TrackerStatisticsView> self(this);const auto status=status_;const auto model=model_;const auto proxy=proxy_;const auto table=table_;
  const auto current=[&]{return self&&self->revision_==revision&&self->packet_==packet&&self->isVisible()&&!self->window()->isMinimized()&&status&&model&&proxy&&table&&table->model()==proxy&&proxy->sourceModel()==model;};
  rendering_=true;struct Finish{QPointer<TrackerStatisticsView> self;quint64 revision;~Finish(){if(self){self->rendering_=false;if(self->dirty_&&self->revision_!=revision){self->queueRefresh();}}}}finish{self,revision};
  if(!static_cast<Model*>(model.data())->replace(packet,current)||!current()){return;}
  proxy->setFilterFixedString(search_);if(!current()){return;}
  QString text=tr("Tracker observations unavailable");
  if(packet&&packet->snapshot){const auto& value=*packet->snapshot;
    if(value.state==core::TrackerReadState::DisabledByIsolation){text=tr("Trackers disabled by the active isolation policy");}
    else if(value.state==core::TrackerReadState::Ready){text=value.rows.empty()?tr("No configured trackers in the observed torrent"):tr("Observed tracker announce state");}
    if(value.truncated){text+=tr("; bounded partial snapshot");}if(!packet->current){text+=tr("; last observed");}}
  status->setText(text);if(!current()){return;}status->setToolTip(tr("Snapshot only; opening this view does not activate any tracker."));if(!current()){return;}
  status->setAccessibleDescription(text);if(!current()){return;}dirty_=false;
}
void TrackerStatisticsView::showEvent(QShowEvent* event){QPointer<TrackerStatisticsView> self(this);QWidget::showEvent(event);if(!self){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();}
bool TrackerStatisticsView::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange)){QPointer<TrackerStatisticsView> self(this);QPointer<QObject> watched(object);setUpdatesEnabled(!window()->isMinimized());if(!self){return watched.isNull();}queueRefresh();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
