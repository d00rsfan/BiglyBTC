#include "azureus_statistics_view.hpp"
#include <QAbstractTableModel>
#include <QEvent>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QShowEvent>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace biglybtc::ui {
namespace {
namespace udp=services::azureus_udp;
QString eventName(udp::OperationEventKind kind){
  switch(kind){case udp::OperationEventKind::Sent:return QObject::tr("Sent");case udp::OperationEventKind::PartialReply:return QObject::tr("Partial reply");
    case udp::OperationEventKind::Reply:return QObject::tr("Reply");case udp::OperationEventKind::RemoteError:return QObject::tr("Remote error");
    case udp::OperationEventKind::Timeout:return QObject::tr("Timeout");case udp::OperationEventKind::Cancelled:return QObject::tr("Cancelled");
    case udp::OperationEventKind::Closed:return QObject::tr("Closed");case udp::OperationEventKind::Abandoned:return QObject::tr("Receiver gone");
    case udp::OperationEventKind::Capacity:return QObject::tr("Capacity");case udp::OperationEventKind::SendFailed:return QObject::tr("Send failed");}
  return QObject::tr("Unknown");
}
QString actionName(std::int32_t action){switch(action){case 1024:return QStringLiteral("PING");case 1026:return QStringLiteral("STORE");case 1028:return QStringLiteral("FIND_NODE");case 1030:return QStringLiteral("FIND_VALUE");default:return QString::number(action);}}
QString familyName(udp::TransportStatisticsFamily family){switch(family){case udp::TransportStatisticsFamily::Unbound:return QObject::tr("Unbound");case udp::TransportStatisticsFamily::IPv4:return QStringLiteral("IPv4");case udp::TransportStatisticsFamily::IPv6:return QStringLiteral("IPv6");}return QObject::tr("Unknown");}
QLabel* plain(QString text,const char* name,QWidget* parent){auto* label=new QLabel(std::move(text),parent);label->setObjectName(QString::fromLatin1(name));label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);return label;}
constexpr std::array<const char*,22> names{"Datagrams accepted for send","Receive dequeue attempts","Decoded requests","Decoded replies","Malformed","Oversized","Unowned endpoint","Foreign reply","Timeouts","Cancelled","Abandoned receivers","Callback errors","I/O errors","Read turns","Delivery turns","Service capacity drops","Service address rejections","Authority errors","Policy errors","Pending transactions","Queued completions","Allowed peers"};
std::array<std::uint64_t,22> values(const udp::OwnedDhtStatisticsSnapshot& source){const auto& s=source.transport();const auto& c=s.counters();return {c.sent,c.received,c.requests,c.replies,c.malformed,c.oversized,c.unowned,c.foreign,c.timeouts,c.cancelled,c.abandoned,c.callbackErrors,c.ioErrors,c.readTurns,c.deliveryTurns,c.serviceCapacityDrops,c.serviceAddressRejected,c.authorityErrors,c.policyErrors,s.pending(),s.completions(),s.allowedPeers()};}
class Model final:public QAbstractTableModel{
public:
  Model(AzureusStatisticsSection kind,QObject* parent):QAbstractTableModel(parent),kind_(kind){}
  int rowCount(const QModelIndex& parent={})const override{if(parent.isValid()||!source_){return 0;}return kind_==AzureusStatisticsSection::Dht?22:kind_==AzureusStatisticsSection::Operations?static_cast<int>(source_->events().size()):0;}
  int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:kind_==AzureusStatisticsSection::Operations?4:2;}
  QVariant headerData(int section,Qt::Orientation orientation,int role)const override{if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole){return {};}
    if(kind_!=AzureusStatisticsSection::Operations){return section==0?tr("Metric"):section==1?tr("Observed value"):QVariant{};}
    switch(section){case 0:return tr("Ticket");case 1:return tr("Operation");case 2:return tr("Event");case 3:return tr("Since send (ns)");default:return {};}}
  QVariant data(const QModelIndex& index,int role)const override{
    if(!index.isValid()||index.model()!=this||index.row()<0||index.row()>=rowCount()||index.column()<0||index.column()>=columnCount()){return {};}
    if(kind_==AzureusStatisticsSection::Dht){const auto row=static_cast<std::size_t>(index.row());const auto value=values(*source_)[row];
      if(role==Qt::UserRole &&index.column()==1){return QVariant::fromValue(static_cast<qulonglong>(value));}
      if(role!=Qt::DisplayRole &&role!=Qt::AccessibleTextRole){return {};}
      return index.column()==0?QVariant(tr(names[row])):QVariant(QString::number(value));}
    const auto& event=source_->events()[static_cast<std::size_t>(index.row())];
    if(role==Qt::UserRole){if(index.column()==0){return QVariant::fromValue(static_cast<qulonglong>(event.ticket));}if(index.column()==3 &&event.elapsedNanoseconds){return QVariant::fromValue(static_cast<qlonglong>(*event.elapsedNanoseconds));}return {};}
    if(role!=Qt::DisplayRole &&role!=Qt::AccessibleTextRole){return {};}
    switch(index.column()){case 0:return QString::number(event.ticket);case 1:return actionName(event.requestAction);case 2:return eventName(event.kind);case 3:return event.elapsedNanoseconds?QString::number(*event.elapsedNanoseconds):tr("Unavailable");default:return {};}
  }
  bool replace(std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source,const std::function<bool()>& current){QPointer<Model> guard(this);beginResetModel();if(!guard){return false;}const bool accepted=current();if(!guard){return false;}if(accepted){source_=std::move(source);}endResetModel();return accepted&&guard;}
private:const AzureusStatisticsSection kind_;std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source_;
};
QString endpointText(const services::azureus_dht::Address& endpoint){
  if(endpoint.length==4){quint32 ip=0;for(unsigned i=0;i<4;++i){ip=(ip<<8)|endpoint.bytes[i];}
    return QHostAddress(ip).toString()+QLatin1Char(':')+QString::number(endpoint.port);}
  if(endpoint.length==16){Q_IPV6ADDR ip{};std::copy_n(endpoint.bytes.begin(),16,ip.c);
    return QLatin1Char('[')+QHostAddress(ip).toString()+QStringLiteral("]:")+QString::number(endpoint.port);}
  return QObject::tr("Endpoint unavailable");
}
QString peerDescription(const udp::VivaldiPeerObservation& value){
  const auto& position=value.position;const auto& c=position.coordinates;
  return QObject::tr("Peer %1; last reported Vivaldi1 x %2 / y %3 / height %4 ms; error %5; actual correlated RTT %6 ns; report %7; ticket %8; owner receive time %9 ns; local update %10. Reported coordinates are latency estimates, not geography or a current-liveness claim.")
      .arg(endpointText(value.endpoint)).arg(c.x,0,'g',9).arg(c.y,0,'g',9).arg(c.h,0,'g',9).arg(position.error,0,'g',9)
      .arg(value.rttNanoseconds).arg(value.sequence).arg(value.ticket).arg(value.observedSteadyNanoseconds)
      .arg(value.localUpdateAccepted?QObject::tr("accepted"):QObject::tr("not accepted"))+QObject::tr(" Actor generation %1.").arg(value.generation);
}
class PeerPositionModel final:public QAbstractTableModel{
public:
  explicit PeerPositionModel(QObject* parent):QAbstractTableModel(parent){}
  int rowCount(const QModelIndex& parent={})const override{return parent.isValid()||!source_||!source_->vivaldiEnabled()?0:static_cast<int>(source_->peerPositions().size());}
  int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:10;}
  QVariant headerData(int section,Qt::Orientation orientation,int role)const override{
    if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole||section<0||section>=10){return {};}
    static constexpr std::array<const char*,10> labels{"Peer endpoint","X (ms)","Y (ms)","Height (ms)","Error","Actual RTT (ns)","Report sequence","Operation ticket","Owner receive time (ns)","Local update"};return tr(labels[static_cast<std::size_t>(section)]);
  }
  QVariant data(const QModelIndex& index,int role)const override{
    if(!index.isValid()||index.model()!=this||index.row()<0||index.row()>=rowCount()||index.column()<0||index.column()>=10){return {};}
    const auto& value=source_->peerPositions()[static_cast<std::size_t>(index.row())];const auto& c=value.position.coordinates;
    if(role==Qt::ToolTipRole||role==Qt::AccessibleDescriptionRole){return peerDescription(value);}
    if(role==Qt::UserRole){switch(index.column()){
      case 1:return c.x;case 2:return c.y;case 3:return c.h;case 4:return value.position.error;
      case 5:return QVariant::fromValue(static_cast<qlonglong>(value.rttNanoseconds));
      case 6:return QVariant::fromValue(static_cast<qulonglong>(value.sequence));case 7:return QVariant::fromValue(static_cast<qulonglong>(value.ticket));
      case 8:return QVariant::fromValue(static_cast<qlonglong>(value.observedSteadyNanoseconds));case 9:return value.localUpdateAccepted;default:return {};}}
    if(role!=Qt::DisplayRole&&role!=Qt::AccessibleTextRole){return {};}
    switch(index.column()){
      case 0:return endpointText(value.endpoint);case 1:return QString::number(c.x,'g',9);case 2:return QString::number(c.y,'g',9);
      case 3:return QString::number(c.h,'g',9);case 4:return QString::number(value.position.error,'g',9);case 5:return QString::number(value.rttNanoseconds);
      case 6:return QString::number(value.sequence);case 7:return QString::number(value.ticket);case 8:return QString::number(value.observedSteadyNanoseconds);
      case 9:return value.localUpdateAccepted?tr("Accepted"):tr("Not accepted");default:return {};}
  }
  bool replace(std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source,const std::function<bool()>& current){
    QPointer<PeerPositionModel> guard(this);beginResetModel();if(!guard){return false;}
    const bool accepted=current();if(!guard){return false;}if(accepted){source_=std::move(source);}endResetModel();return accepted&&guard;
  }
private:std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source_;
};
class RouterModel final:public QAbstractTableModel{
public:
  using Snapshot=services::azureus_router::RouterStatisticsSnapshot;
  explicit RouterModel(QObject* parent):QAbstractTableModel(parent){}
  int rowCount(const QModelIndex& parent={})const override{return parent.isValid()||!source_?0:7;}
  int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:2;}
  QVariant headerData(int section,Qt::Orientation orientation,int role)const override{
    if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole){return {};}
    return section==0?QVariant(tr("Router statistic")):section==1?QVariant(tr("Observed count")):QVariant{};
  }
  QVariant data(const QModelIndex& index,int role)const override{
    if(!index.isValid()||index.model()!=this||index.row()<0||index.row()>=rowCount()||index.column()<0||index.column()>=2){return {};}
    static constexpr std::array<const char*,7> labels{"Nodes","Leaves","Bucket contacts","Replacements","Live contacts","Unknown contacts","Failing contacts"};
    const auto row=static_cast<std::size_t>(index.row());const auto value=source_->counts()[row];
    if(role==Qt::UserRole&&index.column()==1){return QVariant::fromValue(static_cast<qulonglong>(value));}
    if(role!=Qt::DisplayRole&&role!=Qt::AccessibleTextRole){return {};}
    return index.column()==0?QVariant(tr(labels[row])):QVariant(QString::number(value));
  }
  bool replace(std::shared_ptr<const Snapshot> source,const std::function<bool()>& current){
    QPointer<RouterModel> guard(this);beginResetModel();if(!guard){return false;}
    const bool accepted=current();if(!guard){return false;}if(accepted){source_=std::move(source);}
    endResetModel();return accepted&&guard;
  }
private:std::shared_ptr<const Snapshot> source_;
};
}
class AzureusStatisticsView::PositionPlot final:public QWidget{
public:
  using QWidget::QWidget;
  void cache(std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> value,bool current=true){source_=std::move(value);current_=current;}
  QString description()const{
    if(!source_||!source_->vivaldiEnabled()){return tr("Vivaldi peer observations unavailable");}
    const auto& p=source_->localPosition();QString result=tr("Blue: local Vivaldi1 estimate x %1 / y %2 / height %3 ms; error %4. Orange: last reported peer coordinates; retained reports do not establish current liveness or geographic location.")
        .arg(p.coordinates.x,0,'g',9).arg(p.coordinates.y,0,'g',9).arg(p.coordinates.h,0,'g',9).arg(p.error,0,'g',9);
    if(!current_||source_->transport().state()==udp::TransportStatisticsState::Closed){result=tr("Last observed. ")+result;}
    for(const auto& peer:source_->peerPositions()){result+=QLatin1Char('\n')+peerDescription(peer);}
    result+=tr("\n%1 peer reports retained; %2 older reports evicted%3.").arg(source_->peerPositions().size()).arg(source_->droppedPeerPositions())
        .arg(source_->peerPositionsLost()?tr("; additional report loss"):QString{});return result;
  }
protected:
  void paintEvent(QPaintEvent*)override{
    if(!isVisible()||window()->isMinimized()){return;}QPainter painter(this);painter.fillRect(rect(),palette().base());
    const auto source=source_;if(!source||!source->vivaldiEnabled()||(!source->coordinateObservations()&&source->peerPositions().empty())){
      painter.setPen(palette().text().color());painter.drawText(rect(),Qt::AlignCenter,tr("No observed Vivaldi1 coordinates"));return;}
    const auto local=source->localPosition().coordinates;double extent=1;
    const auto include=[&](udp::VivaldiCoordinates c){if(udp::validVivaldiCoordinates(c)){extent=std::max({extent,std::abs(static_cast<double>(c.x)),std::abs(static_cast<double>(c.y))});}};
    include(local);for(const auto& peer:source->peerPositions()){include(peer.position.coordinates);}
    const QRectF area=QRectF(rect()).adjusted(44,28,-20,-40);if(area.width()<=0||area.height()<=0){return;}
    painter.setPen(palette().mid().color());painter.drawRect(area);painter.drawLine(QPointF(area.center().x(),area.top()),QPointF(area.center().x(),area.bottom()));painter.drawLine(QPointF(area.left(),area.center().y()),QPointF(area.right(),area.center().y()));
    painter.setPen(palette().text().color());painter.drawText(QRectF(area.left(),area.bottom()+4,area.width(),25),Qt::AlignCenter,tr("Vivaldi X / Y latency coordinates (ms)"));
    const auto draw=[&](udp::VivaldiCoordinates c,QColor color,bool localPoint){if(!udp::validVivaldiCoordinates(c)){return;}
      const QPointF point(area.center().x()+area.width()*0.45*(static_cast<double>(c.x)/extent),area.center().y()-area.height()*0.45*(static_cast<double>(c.y)/extent));
      painter.setPen(QPen(color,2));painter.setBrush(color);if(localPoint){painter.drawRect(QRectF(point.x()-4,point.y()-4,8,8));}else{painter.drawEllipse(point,4,4);}};
    for(const auto& peer:source->peerPositions()){draw(peer.position.coordinates,QColor(190,95,20),false);}draw(local,QColor(30,125,205),true);
  }
private:std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source_;bool current_=false;
};
class AzureusStatisticsView::OperationTimeline final:public QWidget{
public:
  explicit OperationTimeline(QWidget* parent=nullptr):QWidget(parent){
    setObjectName(QStringLiteral("azureus-operation-timeline"));setAccessibleName(tr("Operation timeline, grouped by transaction ticket"));setMinimumWidth(640);setMinimumHeight(120);
  }
  void cache(std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> value)noexcept{source_=std::move(value);}
  int requiredHeight()const noexcept{return std::max(120,static_cast<int>(tickets().count)*26+68);}
  QString description()const{
    if(!source_||!source_->operationsEnabled()){return tr("Operation observations unavailable");}
    const auto groups=tickets();QString result=tr("Operation timeline; elapsed since each operation started. Missing latency is shown separately, never as zero.");
    for(std::size_t row=0;row<groups.count;++row){result+=tr("\nTicket %1:").arg(groups.values[row]);
      for(const auto& event:source_->events()){if(event.ticket==groups.values[row]){
        result+=QLatin1Char(' ')+actionName(event.requestAction)+QLatin1Char(' ')+eventName(event.kind)+QLatin1Char(' ')
          +(event.elapsedNanoseconds&&*event.elapsedNanoseconds>=0?QString::number(*event.elapsedNanoseconds)+QStringLiteral(" ns"):tr("latency unavailable"))+QLatin1Char(';');}}
    }
    if(source_->droppedEvents()){result+=tr("\n%1 earlier events dropped; visible records are partial.").arg(source_->droppedEvents());}
    return result;
  }
protected:
  void paintEvent(QPaintEvent*)override{
    if(!isVisible()||window()->isMinimized()){return;}const auto source=source_;QPainter p(this);p.fillRect(rect(),palette().base());p.setRenderHint(QPainter::Antialiasing);
    const auto groups=tickets();p.setPen(palette().text().color());
    if(!source||!source->operationsEnabled()||!groups.count){p.drawText(rect(),Qt::AlignCenter,tr("No observed operation events"));return;}
    const QRectF area(180,32,std::max(1,width()-214),static_cast<int>(groups.count)*26);
    std::int64_t maximum=1;for(const auto& event:source->events()){if(event.elapsedNanoseconds&&*event.elapsedNanoseconds>=0){maximum=std::max(maximum,*event.elapsedNanoseconds);}}
    p.drawText(QPointF(area.left(),17),tr("Elapsed per operation (ms); markers are actual events"));
    p.drawText(QPointF(5,17),tr("Ticket / operation"));p.drawText(QPointF(width()-100,17),tr("Unavailable"));
    for(std::size_t row=0;row<groups.count;++row){const auto ticket=groups.values[row];const double y=area.top()+(static_cast<double>(row)+0.5)*26;
      const udp::OperationEvent* first=nullptr;for(const auto& event:source->events()){if(event.ticket==ticket){first=&event;break;}}
      p.setPen(palette().text().color());p.drawText(QPointF(5,y+4),QString::number(ticket)+QLatin1Char(' ')+(first?actionName(first->requestAction):QString{}));
      p.setPen(palette().mid().color());p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
      std::optional<QPointF> previous;
      for(const auto& event:source->events()){if(event.ticket!=ticket){continue;}
        const bool known=event.elapsedNanoseconds&&*event.elapsedNanoseconds>=0;
        const auto x=known?area.left()+static_cast<double>(static_cast<long double>(*event.elapsedNanoseconds)/maximum)*area.width():static_cast<double>(width()-16);
        const QPointF here(x,y);const auto color=eventColor(event.kind);p.setPen(QPen(color,2));p.setBrush(color);
        if(known&&previous){p.drawLine(*previous,here);}
        if(known){p.drawEllipse(here,4,4);previous=here;}
        else{p.setBrush(Qt::NoBrush);p.drawRect(QRectF(x-4,y-4,8,8));previous.reset();}
      }
    }
    p.setPen(palette().text().color());p.drawText(QPointF(area.left(),area.bottom()+23),QStringLiteral("0"));
    p.drawText(QPointF(std::max(area.left(),area.right()-115),area.bottom()+23),QString::number(static_cast<double>(maximum)/1e6,'g',6)+QStringLiteral(" ms"));
  }
private:
  struct Tickets{std::size_t count=0;std::array<std::uint64_t,udp::OwnedDhtStatisticsSnapshot::MaximumEvents> values{};};
  Tickets tickets()const noexcept{
    Tickets result;if(!source_){return result;}for(const auto& event:source_->events()){
      const auto end=result.values.begin()+static_cast<std::ptrdiff_t>(result.count);
      if(std::find(result.values.begin(),end,event.ticket)==end&&result.count<result.values.size()){result.values[result.count++]=event.ticket;}}
    return result;
  }
  static QColor eventColor(udp::OperationEventKind kind){switch(kind){
    case udp::OperationEventKind::Sent:return {30,100,210};case udp::OperationEventKind::PartialReply:return {210,120,20};case udp::OperationEventKind::Reply:return {20,150,80};
    case udp::OperationEventKind::RemoteError:case udp::OperationEventKind::SendFailed:return {190,50,40};case udp::OperationEventKind::Timeout:return {140,70,170};
    case udp::OperationEventKind::Capacity:return {190,110,20};default:return {90,90,90};}}
  std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot> source_;
};
AzureusStatisticsView::AzureusStatisticsView(AzureusStatisticsSection section,QWidget* parent):QWidget(parent),section_(section){
  setObjectName(section==AzureusStatisticsSection::Dht?QStringLiteral("azureus-dht-view"):section==AzureusStatisticsSection::Operations?QStringLiteral("azureus-operations-view"):QStringLiteral("azureus-vivaldi-view"));
  auto* layout=new QVBoxLayout(this);state_=plain(tr("Azureus service observation unavailable"),"azureus-statistics-status",this);layout->addWidget(state_);
  coordinates_=plain(tr("No coordinate observations"),"azureus-vivaldi-coordinates",this);coordinates_->setVisible(section==AzureusStatisticsSection::Vivaldi);layout->addWidget(coordinates_);
  if(section==AzureusStatisticsSection::Operations){
    layout->addWidget(plain(tr("Operation timeline — blue sent, orange partial reply, green reply, red error, purple timeout, grey cancelled/closed. Each row is one transaction, not a routing tree."),"azureus-operation-timeline-scope",this));
    auto* scroll=new QScrollArea(this);scroll->setObjectName(QStringLiteral("azureus-operation-timeline-scroll"));scroll->setWidgetResizable(true);scroll->setMinimumHeight(150);scroll->setMaximumHeight(280);
    timeline_=new OperationTimeline;scroll->setWidget(timeline_);layout->addWidget(scroll,1);
  }
  if(section==AzureusStatisticsSection::Dht){
    sentPayloadBytes_=plain(tr("UDP payload bytes sent: Unavailable"),"azureus-dht-sent-payload-bytes",this);layout->addWidget(sentPayloadBytes_);
    receivedPayloadBytes_=plain(tr("UDP payload bytes received: Unavailable"),"azureus-dht-received-payload-bytes",this);layout->addWidget(receivedPayloadBytes_);
  }
  if(section!=AzureusStatisticsSection::Vivaldi){model_=new Model(section,this);table_=new QTableView(this);table_->setObjectName(QStringLiteral("azureus-statistics-table"));table_->setModel(model_);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table_->setColumnWidth(0,section==AzureusStatisticsSection::Dht?360:150);table_->setColumnWidth(1,200);if(section==AzureusStatisticsSection::Operations){table_->setColumnWidth(2,180);table_->setColumnWidth(3,240);}layout->addWidget(table_,1);}
  else{plot_=new PositionPlot(this);plot_->setObjectName(QStringLiteral("azureus-vivaldi-plot"));plot_->setAccessibleName(tr("Local Vivaldi1 estimate and last-reported peer cloud"));plot_->setMinimumHeight(200);layout->addWidget(plot_,1);
    peerState_=plain(tr("Peer coordinate observations unavailable"),"azureus-vivaldi-peers-status",this);layout->addWidget(peerState_);
    peerModel_=new PeerPositionModel(this);peerTable_=new QTableView(this);peerTable_->setObjectName(QStringLiteral("azureus-vivaldi-peers-table"));
    peerTable_->setModel(peerModel_);peerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);peerTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    peerTable_->setColumnWidth(0,175);peerTable_->setColumnWidth(5,150);peerTable_->setColumnWidth(8,185);layout->addWidget(peerTable_,1);
  }
  if(section==AzureusStatisticsSection::Dht){
    routerState_=plain(tr("Router observation unavailable; no counts inferred"),"azureus-router-status",this);layout->addWidget(routerState_);
    routerModel_=new RouterModel(this);routerTable_=new QTableView(this);routerTable_->setObjectName(QStringLiteral("azureus-router-table"));
    routerTable_->setModel(routerModel_);routerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);routerTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    routerTable_->setColumnWidth(0,360);routerTable_->setColumnWidth(1,200);layout->addWidget(routerTable_,1);
  }
  layout->addWidget(plain(tr("DHT network activity. Vivaldi positions estimate network latency; they are not geographic locations. This preview does not enable public DHT routing or distributed storage."),"azureus-statistics-scope",this));
}
bool AzureusStatisticsView::beginSession(quint64 session){if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}++revision_;session_=session;packet_.reset();if(plot_){plot_->cache({});}if(timeline_){timeline_->cache({});}dirty_=true;refresh();return true;}
bool AzureusStatisticsView::setPresentation(AzureusStatisticsPresentation packet){if(revision_==std::numeric_limits<quint64>::max()||!acceptAzureusStatistics(packet_,packet,session_)){return false;}++revision_;packet_=std::move(packet);if(plot_){plot_->cache(packet_->snapshot,packet_->current);}if(timeline_){timeline_->cache(packet_->snapshot);}dirty_=true;refresh();return true;}
void AzureusStatisticsView::queueRefresh(){if(queued_){return;}queued_=true;QPointer<AzureusStatisticsView> guard(this);QMetaObject::invokeMethod(this,[guard]{if(guard){guard->queued_=false;guard->refresh();}},Qt::QueuedConnection);}
void AzureusStatisticsView::refresh(){
  if(!dirty_||rendering_||!isVisible()||window()->isMinimized()||!state_||!coordinates_){return;}
  QPointer<AzureusStatisticsView> guard(this);const auto packet=packet_;const auto revision=revision_;const auto label=state_,coordinates=coordinates_;const auto table=table_;const auto model=model_;const auto plot=plot_;const auto timeline=timeline_;
  const auto current=[&]{return guard&&guard->revision_==revision&&guard->packet_==packet&&label&&coordinates&&guard->isVisible()&&!guard->window()->isMinimized();};
  rendering_=true;struct Finish{QPointer<AzureusStatisticsView> view;quint64 revision;~Finish(){if(view){view->rendering_=false;if(view->dirty_&&view->revision_!=revision){view->queueRefresh();}}}} finish{guard,revision};
  const auto source=packet?packet->snapshot:std::shared_ptr<const udp::OwnedDhtStatisticsSnapshot>{};
  if(section_!=AzureusStatisticsSection::Vivaldi){if(!table||!model||table->model()!=model){return;}if(!static_cast<Model*>(model.data())->replace(source,current)||!current()){return;}}
  if(section_==AzureusStatisticsSection::Dht){
    const auto sent=sentPayloadBytes_,received=receivedPayloadBytes_;
    if(!sent||!received){return;}
    const auto payloadText=[&](bool upload){
      QString result=upload?tr("UDP payload bytes sent: "):tr("UDP payload bytes received: ");
      if(!source){return result+tr("Unavailable");}
      const auto& transport=source->transport();const auto& counts=transport.counters();
      const auto& bytes=upload?counts.sentPayloadBytes:counts.receivedPayloadBytes;
      result+=bytes?tr("%1 bytes").arg(*bytes):tr("Unknown (overflow or unmeasured discard)");
      if(!packet->current||transport.state()==udp::TransportStatisticsState::Closed){result+=tr("; last observed");}
      else if(transport.state()==udp::TransportStatisticsState::NotStarted){result+=tr("; transport not started");}
      return result;
    };
    const std::array<QPointer<QLabel>,2> payloadLabels{sent,received};
    const std::array<QString,2> payloadValues{payloadText(true),payloadText(false)};
    const auto payloadScope=tr("Actual successful socket-write payload bytes or dequeued UDP payload bytes for this one owned transport. Includes received malformed/unowned data. Excludes UDP/IP overhead; no packet-count estimate or global DHT total. Unknown remains unknown after overflow or an unmeasured discard.");
    for(std::size_t i=0;i<payloadLabels.size();++i){
      const auto target=payloadLabels[i];if(!current()||!target){return;}
      target->setText(payloadValues[i]);if(!current()||!target){return;}
      target->setToolTip(payloadScope);if(!current()||!target){return;}
      target->setAccessibleDescription(payloadValues[i]+QLatin1Char(' ')+payloadScope);if(!current()){return;}
    }
    const auto router=packet?packet->routerSnapshot:std::shared_ptr<const services::azureus_router::RouterStatisticsSnapshot>{};
    const auto routerLabel=routerState_;const auto routerTable=routerTable_;const auto routerModel=routerModel_;
    if(!routerLabel||!routerTable||!routerModel||routerTable->model()!=routerModel){return;}
    if(!static_cast<RouterModel*>(routerModel.data())->replace(router,current)||!current()||!routerLabel){return;}
    QString routerText=tr("Router observation unavailable; no counts inferred");
    if(router){const auto& config=router->config();
      routerText=tr("Owned router; K %1 / B %2 / replacements %3; %4 leaf records. Bounded to %5 contacts / %6 nodes.")
          .arg(config.k).arg(config.b).arg(config.replacements).arg(router->buckets().size())
          .arg(services::azureus_router::MaximumRouterContacts).arg(services::azureus_router::MaximumRouterNodes);
      routerText+=router->limited()?tr(" Partial: %1 capacity drops / %2 lost intents.").arg(router->capacityDrops()).arg(router->lostIntents())
          :tr(" No reported capacity or intent loss.");
      if(router->sleeping()){routerText+=tr(" Sleeping.");}if(router->suspended()){routerText+=tr(" Suspended.");}
      if(!packet->current){routerText+=tr(" Last observed.");}
    }
    routerLabel->setText(routerText);if(!current()||!routerLabel){return;}
    routerLabel->setToolTip(tr("Seven source router counts; local contact included, replacement liveness excluded. No owner or request is started here."));if(!current()||!routerLabel){return;}
    routerLabel->setAccessibleDescription(routerText);if(!current()){return;}
  }
  QString text=tr("Azureus service observation unavailable");
  if(packet){if(packet->state==AzureusStatisticsState::Disabled){text=tr("Azureus service disabled by host policy");}else if(packet->state==AzureusStatisticsState::Unavailable){text=packet->failure.isEmpty()?text:tr("Azureus observation unavailable: %1").arg(packet->failure);}else{
    const auto& transport=source->transport();text=tr("Network %1 / %2; %3").arg(transport.network()).arg(familyName(transport.family())).arg(transport.state()==udp::TransportStatisticsState::NotStarted?tr("not started"):transport.state()==udp::TransportStatisticsState::Closed?tr("closed; last observed"):tr("owned transport observed"));
    if(!packet->current && transport.state()!=udp::TransportStatisticsState::Closed){text+=tr("; last observed");}
    if(section_==AzureusStatisticsSection::Operations){text+=source->operationsEnabled()?tr("; up to64 transaction events, %1 dropped").arg(source->droppedEvents()):tr("; operation observation disabled");}
    if(section_==AzureusStatisticsSection::Vivaldi&&!source->vivaldiEnabled()){text+=tr("; coordinate provider disabled");}
  }}
  label->setText(text);if(!current()){return;}label->setToolTip(tr("No service is started or sampled by this widget."));if(!current()){return;}
  QString coordinateText=tr("No coordinate observations");
  if(source&&source->vivaldiEnabled()&&source->coordinateObservations()){
    const auto& value=source->localPosition();coordinateText=tr("Local Vivaldi1: x %1, y %2, height %3, error %4. Observed RTT %5 ns; %6 accepted observations. Blue: local estimate; orange: last-reported peers.")
        .arg(value.coordinates.x,0,'g',9).arg(value.coordinates.y,0,'g',9).arg(value.coordinates.h,0,'g',9).arg(value.error,0,'g',9).arg(source->lastRttNanoseconds()?QString::number(*source->lastRttNanoseconds()):tr("Unavailable")).arg(source->coordinateObservations());
  }
  if(section_==AzureusStatisticsSection::Vivaldi){
    coordinates->setText(coordinateText);if(!current()||!plot){return;}
    const auto peerLabel=peerState_;const auto peerTable=peerTable_;const auto peerModel=peerModel_;
    if(!peerLabel||!peerTable||!peerModel||peerTable->model()!=peerModel){return;}
    if(!static_cast<PeerPositionModel*>(peerModel.data())->replace(source,current)||!current()||!peerLabel){return;}
    QString peerText=tr("Peer coordinate observations unavailable");
    if(source&&source->vivaldiEnabled()){
      peerText=tr("%1 last-reported peers retained (bound %2); %3 older reports evicted%4. Blue square: local estimate; orange circles: peer reports. Height/error and actual RTT are in the table; these are not geographic positions or proof of current liveness.")
          .arg(source->peerPositions().size()).arg(udp::OwnedDhtStatisticsSnapshot::MaximumPeers).arg(source->droppedPeerPositions())
          .arg(source->peerPositionsLost()?tr("; report loss detected"):QString{});
      if((packet&&!packet->current)||source->transport().state()==udp::TransportStatisticsState::Closed){peerText+=tr(" Last observed.");}
    }
    peerLabel->setText(peerText);if(!current()||!peerLabel||!plot){return;}
    peerLabel->setToolTip(peerText);if(!current()||!peerLabel||!plot){return;}
    peerLabel->setAccessibleDescription(peerText);if(!current()||!plot){return;}
    plot->cache(source,packet&&packet->current);const auto description=plot->description();plot->setAccessibleDescription(description);if(!current()||!plot){return;}
    plot->setToolTip(tr("Blue square: local latency estimate. Orange circles: last-reported peer coordinates. Inspect the peer table for endpoint, actual RTT, height/error and report provenance. No geography or current liveness is inferred."));if(!current()||!plot){return;}plot->update();
  }
  if(section_==AzureusStatisticsSection::Operations){
    if(!current()||!timeline){return;}timeline->setMinimumHeight(timeline->requiredHeight());if(!current()||!timeline){return;}
    const auto description=timeline->description();timeline->setAccessibleDescription(description);if(!current()||!timeline){return;}
    timeline->setToolTip(description);if(!current()||!timeline){return;}timeline->update();
  }
  label->setAccessibleDescription(text);if(!current()){return;}dirty_=false;
}
void AzureusStatisticsView::showEvent(QShowEvent* event){QPointer<AzureusStatisticsView> guard(this);QWidget::showEvent(event);if(!guard){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();}
bool AzureusStatisticsView::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::WindowStateChange||event->type()==QEvent::Show)){QPointer<AzureusStatisticsView> guard(this);setUpdatesEnabled(!window()->isMinimized());if(!guard){return false;}queueRefresh();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
