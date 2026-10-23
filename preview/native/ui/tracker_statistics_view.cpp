#include "tracker_statistics_view.hpp"
#include <QAbstractTableModel>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPainter>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <array>
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_set>

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
class TrackerHistoryPlot final:public QWidget {
public:
  explicit TrackerHistoryPlot(QWidget* parent):QWidget(parent){setObjectName(QStringLiteral("tracker-history-plot"));setMinimumHeight(160);setMaximumHeight(210);setAccessibleName(tr("Observed HTTP announce queue and engine-reported tracker traffic"));}
  void cacheFrame(const TrackerActivityFrame& frame)noexcept{frame_=frame;}
protected:
  void paintEvent(QPaintEvent*)override{
    const auto frame=frame_;QPainter painter(this);painter.fillRect(rect(),palette().base());painter.setRenderHint(QPainter::Antialiasing);
    const auto first=frame.count?frame.points[0].nanoseconds:0,last=frame.count?frame.points[frame.count-1].nanoseconds:0;
    const auto span=static_cast<long double>(last)-static_cast<long double>(first);
    for(int chart=0;chart<2;++chart){
      const double left=chart*width()/2.0;const QRectF area(left+42,24,width()/2.0-56,height()-62);if(area.width()<=0||area.height()<=0){continue;}
      painter.setPen(palette().text().color());painter.setBrush(Qt::NoBrush);painter.drawRect(area);
      painter.drawText(QRectF(left+6,1,width()/2.0-12,20),Qt::AlignLeft|Qt::AlignVCenter,chart?tr("Tracker B/s — received / sent"):tr("Queued HTTP announces"));
      std::int64_t maximum=1;bool observed=false;
      for(std::size_t i=0;i<frame.count;++i){const auto& point=frame.points[i];
        if(chart){for(const auto& value:{point.receivedBytesPerSecond,point.sentBytesPerSecond}){if(value){observed=true;maximum=std::max(maximum,*value);}}}
        else if(point.queuedHttpAnnounces){observed=true;maximum=std::max(maximum,*point.queuedHttpAnnounces);}}
      if(!observed){painter.drawText(area,Qt::AlignCenter,tr("No observations"));continue;}
      const auto position=[&](const TrackerActivityPoint& point,std::int64_t value){const auto x=span>0?(static_cast<long double>(point.nanoseconds)-static_cast<long double>(first))/span:0.5L;
        return QPointF(area.left()+static_cast<double>(x)*area.width(),area.bottom()-4-static_cast<double>(static_cast<long double>(value)/maximum)*(area.height()-8));};
      painter.save();painter.setClipRect(area.adjusted(-3,-3,3,3));
      for(int direction=0;direction<(chart?2:1);++direction){
        const auto color=!chart?QColor(30,100,210):direction?QColor(210,100,20):QColor(30,150,80);painter.setPen(QPen(color,2));painter.setBrush(color);
        for(std::size_t i=0;i<frame.count;++i){const auto& point=frame.points[i];
          const auto value=!chart?point.queuedHttpAnnounces:direction?point.sentBytesPerSecond:point.receivedBytesPerSecond;
          if(!value){continue;}const auto here=position(point,*value);
          const bool connected=!chart?point.queueConnected:direction?point.sentConnected:point.receivedConnected;
          if(i&&connected){const auto& previous=frame.points[i-1];const auto old=!chart?previous.queuedHttpAnnounces:direction?previous.sentBytesPerSecond:previous.receivedBytesPerSecond;
            if(old){painter.drawLine(position(previous,*old),here);}}
          painter.drawEllipse(here,2.5,2.5);
        }
      }
      painter.restore();painter.setPen(palette().text().color());painter.drawText(QRectF(area.left()+4,area.top()+4,area.width()-8,16),Qt::AlignRight|Qt::AlignVCenter,tr("max %1").arg(QString::number(static_cast<double>(maximum),'g',3)));
    }
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(6,height()-29,width()-12,23),Qt::AlignLeft|Qt::AlignVCenter,frame.count
      ?tr("Owner seconds %1–%2; observed samples, gaps retained").arg(static_cast<double>(first)/1e9,0,'f',2).arg(static_cast<double>(last)/1e9,0,'f',2)
      :tr("Actual session responses only"));
  }
private:TrackerActivityFrame frame_;
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
  historyStatus_=new QLabel(tr("Session tracker history unavailable"),this);historyStatus_->setObjectName(QStringLiteral("tracker-history-status"));historyStatus_->setTextFormat(Qt::PlainText);historyStatus_->setWordWrap(true);layout->addWidget(historyStatus_);
  auto* facts=new QHBoxLayout;queueFact_=new QLabel(this);queueFact_->setObjectName(QStringLiteral("tracker-history-queue"));queueFact_->setTextFormat(Qt::PlainText);queueFact_->setWordWrap(true);
  trafficFact_=new QLabel(this);trafficFact_->setObjectName(QStringLiteral("tracker-history-traffic"));trafficFact_->setTextFormat(Qt::PlainText);trafficFact_->setWordWrap(true);facts->addWidget(queueFact_,1);facts->addWidget(trafficFact_,2);layout->addLayout(facts);
  historyPlot_=new TrackerHistoryPlot(this);layout->addWidget(historyPlot_);
  filter_=new QLineEdit(this);filter_->setObjectName(QStringLiteral("tracker-filter"));filter_->setMaxLength(256);filter_->setPlaceholderText(tr("Filter tracker origins"));filter_->setAccessibleName(tr("Filter tracker origins"));layout->addWidget(filter_);
  model_=new Model(this);proxy_=new Proxy(this);proxy_->setSourceModel(model_);proxy_->setFilterKeyColumn(0);proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  table_=new QTableView(this);table_->setObjectName(QStringLiteral("tracker-table"));table_->setAccessibleName(tr("Actual tracker announce state"));table_->setModel(proxy_);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setWordWrap(false);table_->setSortingEnabled(true);table_->sortByColumn(0,Qt::AscendingOrder);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  constexpr std::array<int,13> widths{260,55,60,150,75,75,75,70,65,80,100,155,90};
  for(std::size_t i=0;i<widths.size();++i){table_->setColumnWidth(static_cast<int>(i),widths[i]);}layout->addWidget(table_,1);
  auto* scope=new QLabel(tr("Read-only tracker state from the observed torrent. Missing responses remain unavailable. Reading this page does not announce, scrape or enable trackers. No announce queue is inferred."),this);scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);layout->addWidget(scope);
  connect(filter_,&QLineEdit::textChanged,this,[this](const QString& value){if(revision_==std::numeric_limits<quint64>::max()){return;}++revision_;search_=value;dirty_=true;queueRefresh();});
}
bool TrackerStatisticsView::beginSession(quint64 session){if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}session_=session;++revision_;packet_.reset();runtimePacket_.reset();latestRuntime_.reset();history_={};if(historyPlot_){static_cast<TrackerHistoryPlot*>(historyPlot_.data())->cacheFrame(history_.frame());}dirty_=true;refresh();return true;}
bool TrackerStatisticsView::setPresentation(TrackerStatisticsPresentation p){if(revision_==std::numeric_limits<quint64>::max()||!acceptTrackerStatistics(packet_,p,session_)){return false;}++revision_;packet_=std::move(p);dirty_=true;refresh();return true;}
bool TrackerStatisticsView::setRuntimeMetrics(RuntimeMetricsPresentation packet){
  if(!packet||!session_||packet->session!=session_||!packet->epoch||!packet->sequence||revision_==std::numeric_limits<quint64>::max()
      ||(runtimePacket_&&(packet->sequence<=runtimePacket_->sequence||packet->epoch<runtimePacket_->epoch))){return false;}
  const bool same=runtimePacket_&&packet->epoch==runtimePacket_->epoch;auto history=same?history_:TrackerActivityHistory{};
  if(packet->snapshot){const auto& source=*packet->snapshot;
    if(!source.sequence||source.sequence>packet->sequence||source.ownerElapsedNanoseconds<0||source.metrics.size()>core::MaximumRuntimeMetrics){return false;}
    try{std::unordered_set<std::string_view> names;names.reserve(source.metrics.size());
      for(const auto& metric:source.metrics){if(metric.name.empty()||metric.name.size()>core::MaximumRuntimeMetricNameBytes||metric.name.find('\0')!=std::string::npos||!names.insert(metric.name).second
          ||(metric.type!=core::RuntimeMetricType::Counter&&metric.type!=core::RuntimeMetricType::Gauge)||metric.group!=core::runtimeMetricGroup(metric.name)){return false;}
        const auto text=QString::fromUtf8(metric.name.data(),static_cast<qsizetype>(metric.name.size()));const auto bytes=text.toUtf8();
        if(std::string_view(bytes.data(),static_cast<std::size_t>(bytes.size()))!=metric.name){return false;}}
    }catch(...){return false;}
    if(same&&latestRuntime_){if(source.sequence<latestRuntime_->sequence||source.ownerElapsedNanoseconds<latestRuntime_->ownerElapsedNanoseconds){return false;}
      if(source.sequence==latestRuntime_->sequence&&(source.ownerElapsedNanoseconds!=latestRuntime_->ownerElapsedNanoseconds||source.metrics!=latestRuntime_->metrics)){return false;}}
    if(!history.observe(source,packet->current)){return false;}
  }else{history.gap();}
  ++revision_;runtimePacket_=std::move(packet);history_=history;if(!same){latestRuntime_.reset();}if(runtimePacket_->snapshot){latestRuntime_=runtimePacket_->snapshot;}dirty_=true;
  if(historyPlot_){static_cast<TrackerHistoryPlot*>(historyPlot_.data())->cacheFrame(history_.frame());}refresh();return true;
}
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
  status->setAccessibleDescription(text);if(!current()){return;}
  const auto source=runtimePacket_;const auto& latest=history_.latest();const auto historyNumber=[](const auto& value){return value?QString::number(*value):tr("Unavailable");};
  const std::array<QPointer<QLabel>,3> labels{historyStatus_,queueFact_,trafficFact_};
  const std::array<QString,3> captions{!source||!source->snapshot?tr("Session tracker history unavailable"):source->current?tr("Last reported session tracker observations"):tr("Last observed session tracker observations"),
    tr("Queued HTTP announces: %1").arg(historyNumber(latest.queuedHttpAnnounces)),
    tr("Received / sent B/s: %1 / %2").arg(historyNumber(latest.receivedBytesPerSecond)).arg(historyNumber(latest.sentBytesPerSecond))};
  for(std::size_t i=0;i<labels.size();++i){const auto label=labels[i];if(!current()||!label){return;}label->setText(captions[i]);if(!current()||!label){return;}
    label->setToolTip(tr("Actual session queue gauge and reported tracker bytes; byte reports include estimated protocol costs. Rates are exact floored differences between consecutive timed responses. Missing, skipped or decreasing observations leave gaps. No request rate or lag is inferred."));
    if(!current()||!label){return;}label->setAccessibleDescription(captions[i]);if(!current()||!label){return;}}
  if(historyPlot_){historyPlot_->update();if(!current()){return;}}dirty_=false;
}
void TrackerStatisticsView::showEvent(QShowEvent* event){QPointer<TrackerStatisticsView> self(this);QWidget::showEvent(event);if(!self){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();}
bool TrackerStatisticsView::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange)){QPointer<TrackerStatisticsView> self(this);QPointer<QObject> watched(object);setUpdatesEnabled(!window()->isMinimized());if(!self){return watched.isNull();}queueRefresh();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
