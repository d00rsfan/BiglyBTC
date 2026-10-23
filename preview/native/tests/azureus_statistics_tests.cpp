#include "azureus_statistics_view.hpp"
#include "owned_transport.hpp"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QTimer>
#include <QUdpSocket>
#include <QTableView>
#include <QtTest>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>
#include <stdexcept>
#include <set>

namespace {
using namespace biglybtc;
namespace udp=services::azureus_udp;
using namespace std::chrono_literals;
template<class T>T& child(QObject& parent,const char* name){auto* found=parent.findChild<T*>(QString::fromLatin1(name));if(!found){throw std::runtime_error("Missing Azureus statistics widget");}return *found;}
struct Owners{
  udp::OwnedPingTransport server,client;QObject receiver;
  explicit Owners(bool reply=true,bool coordinates=true):server([&]{udp::Config c;c.servePings=reply;c.observeOperations=true;c.enableVivaldi=coordinates;return c;}()),
    client([&]{udp::Config c;c.servePings=reply;c.observeOperations=true;c.enableVivaldi=coordinates;return c;}()){
    if(!server.start()||!client.start()||!server.allowPeer(client.endpoint())||!client.allowPeer(server.endpoint())){throw std::runtime_error("Owned numeric-loopback fixture setup failed");}
  }
  auto pull(quint64 sequence=1,quint64 session=7){auto result=client.dhtStatisticsSnapshot();if(!result){throw std::runtime_error("Actual owner snapshot failed");}
    auto packet=std::make_shared<ui::AzureusStatisticsPacket>();packet->session=session;packet->sequence=sequence;packet->state=ui::AzureusStatisticsState::Observed;
    packet->snapshot=std::make_shared<const udp::OwnedDhtStatisticsSnapshot>(std::move(*result));return packet;}
  bool ping(int& calls){return client.ping(server.endpoint(),55,&receiver,[&calls](udp::Outcome){++calls;},2s).has_value();}
};
void show(QWidget& view){view.resize(1150,750);view.show();QCoreApplication::processEvents();}
auto operationPacket(const udp::OwnedDhtStatisticsSnapshot& source,quint64 sequence=1){
  auto packet=std::make_shared<ui::AzureusStatisticsPacket>();packet->session=7;packet->sequence=sequence;packet->state=ui::AzureusStatisticsState::Observed;
  packet->snapshot=std::make_shared<const udp::OwnedDhtStatisticsSnapshot>(source);return packet;
}
int greenPixelsNear(const QImage& image,QPointF logical){
  const auto dpr=image.devicePixelRatio();const int x=qRound(logical.x()*dpr),y=qRound(logical.y()*dpr),radius=qCeil(6*dpr);int count=0;
  for(int py=std::max(0,y-radius);py<std::min(image.height(),y+radius+1);++py){for(int px=std::max(0,x-radius);px<std::min(image.width(),x+radius+1);++px){
    const auto c=image.pixelColor(px,py);if(c.green()>100&&c.green()>c.red()+40&&c.green()>c.blue()+15){++count;}}}return count;
}
class Hook final:public QObject{
public:bool armed=true;std::function<void()> callback;
 bool eventFilter(QObject*,QEvent* event)override{if(armed&&event->type()==QEvent::ToolTipChange){armed=false;callback();return true;}return false;}
};
}
class AzureusStatisticsTests final:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
 void sourceVivaldiHeightArithmeticAndFirstObservationAreRealAlgorithmSteps(){
   udp::VivaldiPosition position;QCOMPARE(position.value().error,10.0f);QCOMPARE(position.observations(),std::uint64_t{0});
   QVERIFY(position.observe(10,udp::VivaldiValue{},[]{return 0.5f;}));QCOMPARE(position.value().error,7.75f);
   QVERIFY(udp::validVivaldiValue(position.value()));QVERIFY(position.value().coordinates.x<0&&position.value().coordinates.y<0&&position.value().coordinates.h>0);
   const auto difference=udp::vivaldiSubtract({1,2,3},{4,6,7});QCOMPARE(difference.x,-3.0f);QCOMPARE(difference.y,-4.0f);QCOMPARE(difference.h,10.0f);
   QCOMPARE(udp::vivaldiMeasure(difference),15.0f); // Height uses abs(h1+h2), not an invented Euclidean third axis.
 }
 void invalidInputAndBoundedRandomFailureLeaveCoordinateOwnerUntouched(){
   for(const float rtt:{0.0f,-1.0f,300001.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
     udp::VivaldiPosition value;QVERIFY(!value.observe(rtt,udp::VivaldiValue{},[]{return 0.5f;}));QCOMPARE(value.observations(),std::uint64_t{0});QCOMPARE(value.value().error,10.0f);}
   udp::VivaldiPosition value;const auto before=value.value();QVERIFY(!value.observe(10,{{30001,0,0},1},[]{return 0.5f;}));
   unsigned calls=0;QVERIFY(!value.observe(10,{},[&]{++calls;return 0.0f;}));QCOMPARE(calls,27U);QVERIFY(value.value()==before);
   QVERIFY(!value.observe(10,{},[]()->float{throw std::runtime_error("Owned RNG failure");}));QVERIFY(value.value()==before);
 }
 void sourceConvergenceAndInvalidResultResetAreBounded(){
   udp::VivaldiPosition value;for(unsigned i=0;i<6;++i){QVERIFY(value.observe(20,{{1,2,3},1},[]{return 0.5f;}));}
   QCOMPARE(value.observations(),std::uint64_t{6});QCOMPARE(value.convergences(),std::uint64_t{1});QVERIFY(udp::validVivaldiValue(value.value()));
   udp::VivaldiPosition reset;QVERIFY(reset.observe(300000,{{1,2,3},std::nextafter(-10.0f,0.0f)},[]{return 0.5f;}));
   QVERIFY(reset.value().coordinates==udp::VivaldiCoordinates{});QCOMPARE(reset.value().error,10.0f);
 }
 void existingOwnerPolicyDefaultsPreserveEmptyWirePositionsAndNoEventHistory(){
   udp::Config serve;serve.servePings=true;udp::OwnedPingTransport server(std::move(serve)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start());QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;int calls=0;bool empty=false;
   QVERIFY(client.ping(server.endpoint(),55,&receiver,[&](udp::Outcome reply){++calls;if(reply){empty=std::get<udp::packet::ReplyBody>(reply->body()).positions.empty();}},2s));
   QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);QVERIFY(empty);const auto sample=client.dhtStatisticsSnapshot();QVERIFY(sample);
   QVERIFY(!sample->operationsEnabled()&&!sample->vivaldiEnabled()&&sample->events().empty());QCOMPARE(sample->coordinateObservations(),std::uint64_t{0});
 }
 void actualPingOwnersProduceRttCoordinatesAndTransactionEvents(){
   Owners owner;int calls=0;QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);const auto first=owner.pull();
   QVERIFY(first->snapshot->coordinateObservations()==1);QVERIFY(first->snapshot->lastRttNanoseconds()&&*first->snapshot->lastRttNanoseconds()>0);
   QVERIFY(first->snapshot->remotePosition());QVERIFY(udp::validVivaldiValue(first->snapshot->localPosition()));
   QVERIFY(first->snapshot->localPosition().coordinates.x!=0||first->snapshot->localPosition().coordinates.y!=0||first->snapshot->localPosition().coordinates.h!=0);
   QCOMPARE(first->snapshot->events().size(),std::size_t{2});QCOMPARE(first->snapshot->events()[0].kind,udp::OperationEventKind::Sent);QCOMPARE(first->snapshot->events()[1].kind,udp::OperationEventKind::Reply);
   QCOMPARE(first->snapshot->events()[0].ticket,first->snapshot->events()[1].ticket);QVERIFY(first->snapshot->events()[1].elapsedNanoseconds&&*first->snapshot->events()[1].elapsedNanoseconds>0);
   int reverse=0;QVERIFY(owner.server.ping(owner.client.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(result){++reverse;}},2s));QTRY_COMPARE_WITH_TIMEOUT(reverse,1,3000);
   const auto server=owner.server.dhtStatisticsSnapshot();QVERIFY(server);QCOMPARE(server->coordinateObservations(),std::uint64_t{1});
   QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,2,3000);const auto next=owner.pull(2);QCOMPARE(next->snapshot->coordinateObservations(),std::uint64_t{2});
   QCOMPARE(first->snapshot->coordinateObservations(),std::uint64_t{1}); // Fixed immutable old observation survives real owner updates.
 }
 void actualPendingOutcomesAndRingOverflowDoNotChangeCallbacks(){
   Owners owner(false);int timeout=0;QVERIFY(owner.client.ping(owner.server.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(!result&&result.error()==udp::Failure::Timeout){++timeout;}},30ms));QTRY_COMPARE_WITH_TIMEOUT(timeout,1,2000);
   auto sample=owner.pull();QCOMPARE(sample->snapshot->events().back().kind,udp::OperationEventKind::Timeout);QCOMPARE(sample->snapshot->coordinateObservations(),std::uint64_t{0});
   ui::AzureusStatisticsView timeline(ui::AzureusStatisticsSection::Operations);QVERIFY(timeline.beginSession(7));show(timeline);QVERIFY(timeline.setPresentation(sample));
   QVERIFY(child<QWidget>(timeline,"azureus-operation-timeline").accessibleDescription().contains(QStringLiteral("Timeout")));
   int cancelled=0;for(unsigned i=0;i<40;++i){const auto ticket=owner.client.ping(owner.server.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(!result&&result.error()==udp::Failure::Cancelled){++cancelled;}},2s);QVERIFY(ticket);QVERIFY(owner.client.cancel(*ticket));}
   QTRY_COMPARE(cancelled,40);sample=owner.pull(2);QCOMPARE(sample->snapshot->events().size(),std::size_t{64});QCOMPARE(sample->snapshot->droppedEvents(),std::uint64_t{18});
   QCOMPARE(sample->snapshot->events().back().kind,udp::OperationEventKind::Cancelled);QVERIFY(sample->snapshot->events().front().serial>1);
   QVERIFY(timeline.setPresentation(sample));std::set<std::uint64_t> tickets;for(const auto& event:sample->snapshot->events()){tickets.insert(event.ticket);}
   const auto description=child<QWidget>(timeline,"azureus-operation-timeline").accessibleDescription();QCOMPARE(description.count(QStringLiteral("\nTicket ")),static_cast<qsizetype>(tickets.size()));
   QVERIFY(description.contains(QStringLiteral("Cancelled")));QVERIFY(description.contains(QStringLiteral("18 earlier events dropped")));
   auto* receiver=new QObject;QVERIFY(owner.client.ping(owner.server.endpoint(),55,receiver,[](udp::Outcome){},2s));delete receiver;sample=owner.pull(3);QCOMPARE(sample->snapshot->events().back().kind,udp::OperationEventKind::Abandoned);
   int closed=0;QVERIFY(owner.client.ping(owner.server.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(!result&&result.error()==udp::Failure::Closed){++closed;}},2s));owner.client.close();QTRY_COMPARE(closed,1);
   sample=owner.pull(4);QCOMPARE(sample->snapshot->events().back().kind,udp::OperationEventKind::Closed);QCOMPARE(sample->snapshot->transport().state(),udp::TransportStatisticsState::Closed);
   QVERIFY(timeline.setPresentation(sample));QVERIFY(child<QWidget>(timeline,"azureus-operation-timeline").accessibleDescription().contains(QStringLiteral("Closed")));
 }
 void actualPartialAndRemoteErrorEventsKeepTheirDistinctProtocolMeaning(){
   udp::Config config;config.observeOperations=true;udp::FindValueSlice part;part.continuation=true;part.values=std::vector<udp::lookup::values::Value>{};
   udp::FindValueSlice final;final.values=std::vector<udp::lookup::values::Value>{};config.findValueReply=std::vector<udp::FindValueSlice>{part,final};
   udp::OwnedPingTransport server(std::move(config));udp::Config observing;observing.observeOperations=true;udp::OwnedPingTransport client(std::move(observing));
   QVERIFY(server.start());QVERIFY(client.start());QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;int replies=0;
   QVERIFY(client.findValue(server.endpoint(),55,udp::lookup::nodes::NodeId{},0,255,&receiver,[&](udp::FindValueOutcome result){if(result){++replies;}},2s));
   QTRY_COMPARE_WITH_TIMEOUT(replies,2,3000);const auto stream=client.dhtStatisticsSnapshot();QVERIFY(stream);QCOMPARE(stream->events().size(),std::size_t{3});
   QCOMPARE(stream->events()[1].kind,udp::OperationEventKind::PartialReply);QCOMPARE(stream->events()[2].kind,udp::OperationEventKind::Reply);
   ui::AzureusStatisticsView timeline(ui::AzureusStatisticsSection::Operations);QVERIFY(timeline.beginSession(7));show(timeline);QVERIFY(timeline.setPresentation(operationPacket(*stream)));
   const auto partial=child<QWidget>(timeline,"azureus-operation-timeline").accessibleDescription();QVERIFY(partial.contains(QStringLiteral("Partial reply")));QCOMPARE(partial.count(QStringLiteral("\nTicket ")),1);
   udp::Config errors;udp::ErrorReplySnapshot error;error.type=2;error.request="key";error.signature="sig";errors.storeErrorReply=error;udp::OwnedPingTransport remote(std::move(errors));
   QVERIFY(remote.start());QVERIFY(remote.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(remote.endpoint()));int terminal=0;
   QVERIFY(client.store(remote.endpoint(),55,udp::StoreRequest{},&receiver,[&](udp::StoreOutcome result){if(result&&std::holds_alternative<udp::remote_error::Decoded>(*result)){++terminal;}},2s));
   QTRY_COMPARE_WITH_TIMEOUT(terminal,1,3000);const auto failure=client.dhtStatisticsSnapshot();QVERIFY(failure);QCOMPARE(failure->events().back().kind,udp::OperationEventKind::RemoteError);
   QCOMPARE(failure->transport().counters().replies,std::uint64_t{3});QCOMPARE(client.pendingCount(),std::size_t{0});
   QVERIFY(timeline.setPresentation(operationPacket(*failure,2)));QVERIFY(child<QWidget>(timeline,"azureus-operation-timeline").accessibleDescription().contains(QStringLiteral("Remote error")));
 }
 void actualSnapshotFeedsAllThreePassiveViewsWithoutMoreNetworkWork(){
   Owners owner;int calls=0;QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);const auto packet=owner.pull();const auto sent=owner.client.statistics().sent;
   for(const auto section:{ui::AzureusStatisticsSection::Dht,ui::AzureusStatisticsSection::Operations,ui::AzureusStatisticsSection::Vivaldi}){
     ui::AzureusStatisticsView view(section);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet));QVERIFY(view.presentation()==packet);
     auto& status=child<QLabel>(view,"azureus-statistics-status");QVERIFY(status.text().contains("Network 4 / IPv4"));QCOMPARE(status.textFormat(),Qt::PlainText);
     if(section==ui::AzureusStatisticsSection::Dht){auto* model=child<QTableView>(view,"azureus-statistics-table").model();QCOMPARE(model->rowCount(),22);QCOMPARE(model->index(0,1).data(Qt::UserRole).toULongLong(),1ULL);}
     if(section==ui::AzureusStatisticsSection::Operations){auto* model=child<QTableView>(view,"azureus-statistics-table").model();QCOMPARE(model->rowCount(),2);QCOMPARE(model->index(1,2).data().toString(),QStringLiteral("Reply"));QVERIFY(model->index(1,3).data(Qt::UserRole).toLongLong()>0);
       auto& timeline=child<QWidget>(view,"azureus-operation-timeline");const auto description=timeline.accessibleDescription();QCOMPARE(description.count(QStringLiteral("\nTicket ")),1);
       QVERIFY(description.contains(QStringLiteral("Ticket %1:").arg(packet->snapshot->events()[0].ticket)));QVERIFY(description.contains(QStringLiteral("Sent")));QVERIFY(description.contains(QStringLiteral("Reply")));
       for(const auto& event:packet->snapshot->events()){QVERIFY(event.elapsedNanoseconds);QVERIFY(description.contains(QString::number(*event.elapsedNanoseconds)+QStringLiteral(" ns")));}
       const auto image=timeline.grab().toImage();QVERIFY(!image.isNull());QVERIFY(greenPixelsNear(image,QPointF(timeline.width()-34,45))>5);
       const auto capture=qEnvironmentVariable("BIGLYBTC_AZUREUS_OPERATION_CAPTURE_DIRECTORY");if(!capture.isEmpty()){QVERIFY(QDir().mkpath(capture));QVERIFY(view.grab().save(capture+QStringLiteral("/azureus-operation-timeline.png")));}
     }
     if(section==ui::AzureusStatisticsSection::Vivaldi){QVERIFY(child<QLabel>(view,"azureus-vivaldi-coordinates").text().contains("1 accepted observations"));QVERIFY(!child<QWidget>(view,"azureus-vivaldi-plot").grab().isNull());}
     QVERIFY(view.findChildren<QTimer*>().isEmpty());
   }
   QCOMPARE(owner.client.statistics().sent,sent);QCOMPARE(owner.client.pendingCount(),std::size_t{0});
 }
 void disabledUnavailableAndNewSessionClearActualFactsWithPlainErrors(){
   Owners owner;ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Dht);view.beginSession(7);show(view);const auto actual=owner.pull();QVERIFY(view.setPresentation(actual));
   auto disabled=std::make_shared<ui::AzureusStatisticsPacket>();disabled->session=7;disabled->sequence=2;disabled->state=ui::AzureusStatisticsState::Disabled;QVERIFY(view.setPresentation(disabled));QCOMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),0);
   QVERIFY(child<QLabel>(view,"azureus-statistics-status").text().contains("disabled"));auto failed=std::make_shared<ui::AzureusStatisticsPacket>(*disabled);failed->sequence=3;failed->state=ui::AzureusStatisticsState::Unavailable;failed->failure="<b>not markup</b>";
   QVERIFY(view.setPresentation(failed));QVERIFY(child<QLabel>(view,"azureus-statistics-status").text().contains(failed->failure));QCOMPARE(child<QLabel>(view,"azureus-statistics-status").textFormat(),Qt::PlainText);
   QVERIFY(!view.setPresentation(actual));QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(!view.setPresentation(owner.pull(4,7)));QVERIFY(view.setPresentation(owner.pull(1,8)));
 }
 void hiddenMinimizedAndTooltipReentryRetainLatestOwnedObservation(){
   Owners owner;ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Operations);view.beginSession(7);show(view);QVERIFY(view.setPresentation(owner.pull()));const auto oldTimeline=child<QWidget>(view,"azureus-operation-timeline").accessibleDescription();view.hide();int calls=0;QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);
   const auto latest=owner.pull(2);QVERIFY(view.setPresentation(latest));QCOMPARE(child<QWidget>(view,"azureus-operation-timeline").accessibleDescription(),oldTimeline);QCOMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),0);show(view);QTRY_COMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),2);
   QTRY_VERIFY(child<QWidget>(view,"azureus-operation-timeline").accessibleDescription().contains(QStringLiteral("Reply")));
   view.showMinimized();QTRY_VERIFY(view.isMinimized());auto disabled=std::make_shared<ui::AzureusStatisticsPacket>();disabled->session=7;disabled->sequence=3;disabled->state=ui::AzureusStatisticsState::Disabled;QVERIFY(view.setPresentation(disabled));QCoreApplication::processEvents();QCOMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),2);
   view.showNormal();QTRY_COMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),0);
   child<QLabel>(view,"azureus-statistics-status").setToolTip({});Hook rebind;rebind.callback=[&]{view.beginSession(8);};child<QLabel>(view,"azureus-statistics-status").installEventFilter(&rebind);QVERIFY(view.setPresentation(owner.pull(4)));QTRY_VERIFY(!view.presentation());
 }
 void actualOwnedPayloadBytesKeepZeroPositiveAndRetainedDistinctWithoutChangingTableRows(){
   Owners owner;ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Dht);QVERIFY(view.beginSession(7));show(view);
   const auto empty=owner.pull();QVERIFY(empty->snapshot->transport().counters().sentPayloadBytes);QVERIFY(empty->snapshot->transport().counters().receivedPayloadBytes);
   QCOMPARE(*empty->snapshot->transport().counters().sentPayloadBytes,std::uint64_t{0});QVERIFY(view.setPresentation(empty));
   auto& sent=child<QLabel>(view,"azureus-dht-sent-payload-bytes");auto& received=child<QLabel>(view,"azureus-dht-received-payload-bytes");
   QCOMPARE(sent.text(),QStringLiteral("UDP payload bytes sent: 0 bytes"));QCOMPARE(received.text(),QStringLiteral("UDP payload bytes received: 0 bytes"));
   int calls=0;QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);const auto packet=owner.pull(2);const auto& bytes=packet->snapshot->transport().counters();
   QVERIFY(bytes.sentPayloadBytes&&*bytes.sentPayloadBytes>0);QVERIFY(bytes.receivedPayloadBytes&&*bytes.receivedPayloadBytes>0);QVERIFY(view.setPresentation(packet));
   QCOMPARE(sent.text(),QStringLiteral("UDP payload bytes sent: %1 bytes").arg(*bytes.sentPayloadBytes));QCOMPARE(received.text(),QStringLiteral("UDP payload bytes received: %1 bytes").arg(*bytes.receivedPayloadBytes));
   QCOMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),22);QCOMPARE(sent.textFormat(),Qt::PlainText);QCOMPARE(received.textFormat(),Qt::PlainText);
   QVERIFY(sent.toolTip().contains(QStringLiteral("Excludes UDP/IP overhead")));QVERIFY(received.accessibleDescription().contains(QStringLiteral("no packet-count estimate or global DHT total")));
   const auto before=owner.client.statistics().sent;auto retained=std::make_shared<ui::AzureusStatisticsPacket>(*packet);retained->sequence=3;retained->current=false;QVERIFY(view.setPresentation(retained));
   QVERIFY(sent.text().contains(QStringLiteral("last observed")));QVERIFY(received.text().contains(QStringLiteral("last observed")));QCOMPARE(owner.client.statistics().sent,before);
   owner.client.close();const auto closed=owner.pull(4);QVERIFY(view.setPresentation(closed));QVERIFY(sent.text().contains(QStringLiteral("last observed")));
   QCOMPARE(*closed->snapshot->transport().counters().sentPayloadBytes,*bytes.sentPayloadBytes);QVERIFY(view.findChildren<QTimer*>().isEmpty());
 }
 void actualDiscardMakesReceivedBytesUnknownAndHiddenProfileChangesStayFenced(){
   Owners owner;ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Dht);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(owner.pull()));
   auto& received=child<QLabel>(view,"azureus-dht-received-payload-bytes");const auto zero=received.text();view.hide();
   QUdpSocket sender;const QByteArray oversized(1401,'x');QCOMPARE(sender.writeDatagram(oversized,owner.client.endpoint().address,owner.client.endpoint().port),qint64{1401});
   QTRY_COMPARE_WITH_TIMEOUT(owner.client.statistics().oversized,std::uint64_t{1},3000);const auto unknown=owner.pull(2);QVERIFY(!unknown->snapshot->transport().counters().receivedPayloadBytes);QVERIFY(view.setPresentation(unknown));
   QCOMPARE(received.text(),zero);show(view);QTRY_VERIFY(received.text().contains(QStringLiteral("Unknown (overflow or unmeasured discard)")));
   const auto unknownText=received.text();int calls=0;QVERIFY(owner.ping(calls));QTRY_COMPARE_WITH_TIMEOUT(calls,1,3000);const auto next=owner.pull(3);QVERIFY(!next->snapshot->transport().counters().receivedPayloadBytes);QVERIFY(view.setPresentation(next));QCOMPARE(received.text(),unknownText);
   view.hide();QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(!view.setPresentation(next));show(view);QTRY_COMPARE(received.text(),QStringLiteral("UDP payload bytes received: Unavailable"));
   Owners fresh;const auto actual=fresh.pull(1,8);QVERIFY(view.setPresentation(actual));QCOMPARE(received.text(),QStringLiteral("UDP payload bytes received: 0 bytes"));
 }
 void payloadFactCallbacksMayReplaceTheProfileOrDeleteTheOwningView(){
   Owners owner;ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Dht);QVERIFY(view.beginSession(7));show(view);
   Hook rebind;rebind.callback=[&]{QVERIFY(view.beginSession(8));QVERIFY(view.setPresentation(owner.pull(1,8)));};
   child<QLabel>(view,"azureus-dht-sent-payload-bytes").setToolTip({});child<QLabel>(view,"azureus-dht-sent-payload-bytes").installEventFilter(&rebind);QVERIFY(view.setPresentation(owner.pull()));QVERIFY(!rebind.armed);
   QTRY_VERIFY(view.presentation()&&view.presentation()->session==8);QTRY_COMPARE(child<QLabel>(view,"azureus-dht-received-payload-bytes").text(),QStringLiteral("UDP payload bytes received: 0 bytes"));
   QPointer<ui::AzureusStatisticsView> victim=new ui::AzureusStatisticsView(ui::AzureusStatisticsSection::Dht);QVERIFY(victim->beginSession(7));show(*victim);
   Hook remove;remove.callback=[&]{delete victim.data();};child<QLabel>(*victim,"azureus-dht-sent-payload-bytes").setToolTip({});child<QLabel>(*victim,"azureus-dht-sent-payload-bytes").installEventFilter(&remove);
   victim->setPresentation(owner.pull(2));QVERIFY(victim.isNull());QVERIFY(!remove.armed);
 }
 void actualReportedPeerCloudShowsEndpointCoordinatesAndCorrelatedRtt(){
   Owners owner;udp::Config config;config.servePings=true;config.enableVivaldi=true;udp::OwnedPingTransport second(std::move(config));
   QVERIFY(second.start());QVERIFY(second.allowPeer(owner.client.endpoint()));QVERIFY(owner.client.allowPeer(second.endpoint()));int primed=0;
   QVERIFY(owner.server.ping(owner.client.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(result){++primed;}},2s));QTRY_COMPARE_WITH_TIMEOUT(primed,1,3000);
   QVERIFY(second.ping(owner.client.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(result){++primed;}},2s));QTRY_COMPARE_WITH_TIMEOUT(primed,2,3000);
   int replies=0;QVERIFY(owner.ping(replies));QVERIFY(owner.client.ping(second.endpoint(),55,&owner.receiver,[&](udp::Outcome result){if(result){++replies;}},2s));QTRY_COMPARE_WITH_TIMEOUT(replies,2,3000);
   const auto packet=owner.pull();QCOMPARE(packet->snapshot->peerPositions().size(),std::size_t{2});QCOMPARE(packet->snapshot->droppedPeerPositions(),std::uint64_t{0});
   ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Vivaldi);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet));
   auto* model=child<QTableView>(view,"azureus-vivaldi-peers-table").model();QCOMPARE(model->rowCount(),2);QCOMPARE(model->columnCount(),10);
   auto& plot=child<QWidget>(view,"azureus-vivaldi-plot");const auto description=plot.accessibleDescription();
   for(int row=0;row<2;++row){const auto& report=packet->snapshot->peerPositions()[static_cast<std::size_t>(row)];
     QVERIFY(model->index(row,0).data().toString().endsWith(QLatin1Char(':')+QString::number(report.endpoint.port)));
     QCOMPARE(model->index(row,1).data(Qt::UserRole).toFloat(),report.position.coordinates.x);QCOMPARE(model->index(row,2).data(Qt::UserRole).toFloat(),report.position.coordinates.y);
     QCOMPARE(model->index(row,3).data(Qt::UserRole).toFloat(),report.position.coordinates.h);QCOMPARE(model->index(row,4).data(Qt::UserRole).toFloat(),report.position.error);
     QCOMPARE(model->index(row,5).data(Qt::UserRole).toLongLong(),static_cast<qlonglong>(report.rttNanoseconds));QVERIFY(report.rttNanoseconds>0);
     QCOMPARE(model->index(row,6).data(Qt::UserRole).toULongLong(),static_cast<qulonglong>(report.sequence));QVERIFY(report.sequence>0);
     QCOMPARE(model->index(row,7).data(Qt::UserRole).toULongLong(),static_cast<qulonglong>(report.ticket));QVERIFY(report.ticket>0);
     QCOMPARE(model->index(row,8).data(Qt::UserRole).toLongLong(),static_cast<qlonglong>(report.observedSteadyNanoseconds));
     QCOMPARE(model->index(row,9).data(Qt::UserRole).toBool(),report.localUpdateAccepted);
     QVERIFY(description.contains(QString::number(report.rttNanoseconds)+QStringLiteral(" ns")));QVERIFY(model->index(row,0).data(Qt::AccessibleDescriptionRole).toString().contains(QStringLiteral("not geography")));
   }
   QVERIFY(description.contains(QStringLiteral("Blue: local")));QVERIFY(description.contains(QStringLiteral("Orange: last reported")));QVERIFY(!plot.grab().isNull());
   plot.setMinimumSize(0,0);for(const auto& size:{QSize(1,1),QSize(80,40),QSize(300,200)}){plot.resize(size);QVERIFY(!plot.grab().isNull());}
   auto retained=std::make_shared<ui::AzureusStatisticsPacket>(*packet);retained->sequence=2;retained->current=false;QVERIFY(view.setPresentation(retained));
   QVERIFY(plot.accessibleDescription().startsWith(QStringLiteral("Last observed.")));QVERIFY(child<QLabel>(view,"azureus-vivaldi-peers-status").text().contains(QStringLiteral("Last observed.")));
 }
 void actualPeerCapacityEvictionIsShownWithoutInventingUnobservedPoints(){
   udp::Config observing;observing.enableVivaldi=true;udp::OwnedPingTransport client(std::move(observing));QVERIFY(client.start());QObject receiver;
   std::vector<std::unique_ptr<udp::OwnedPingTransport>> servers;servers.reserve(65);int replies=0;
   for(unsigned i=0;i<65;++i){udp::Config config;config.servePings=true;config.enableVivaldi=true;auto server=std::make_unique<udp::OwnedPingTransport>(std::move(config));
     QVERIFY(server->start());QVERIFY(server->allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server->endpoint()));servers.push_back(std::move(server));}
   for(std::size_t i=0;i<64;++i){QVERIFY(client.ping(servers[i]->endpoint(),55,&receiver,[&](udp::Outcome result){if(result){++replies;}},2s));}
   QTRY_COMPARE_WITH_TIMEOUT(replies,64,3000);const auto first=client.dhtStatisticsSnapshot();QVERIFY(first);QCOMPARE(first->peerPositions().size(),std::size_t{64});
   QVERIFY(client.ping(servers[64]->endpoint(),55,&receiver,[&](udp::Outcome result){if(result){++replies;}},2s));QTRY_COMPARE_WITH_TIMEOUT(replies,65,3000);
   const auto last=client.dhtStatisticsSnapshot();QVERIFY(last);QCOMPARE(last->peerPositions().size(),std::size_t{64});QCOMPARE(last->droppedPeerPositions(),std::uint64_t{1});
   QCOMPARE(first->droppedPeerPositions(),std::uint64_t{0});QCOMPARE(first->peerPositions().size(),std::size_t{64});
   ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Vivaldi);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(operationPacket(*last)));
   QCOMPARE(child<QTableView>(view,"azureus-vivaldi-peers-table").model()->rowCount(),64);
   const auto text=child<QLabel>(view,"azureus-vivaldi-peers-status").text();QVERIFY(text.contains(QStringLiteral("bound 64")));QVERIFY(text.contains(QStringLiteral("1 older reports evicted")));
   QVERIFY(text.contains(QStringLiteral("not geographic positions or proof of current liveness")));QVERIFY(view.findChildren<QTimer*>().isEmpty());
 }
 void peerCloudHiddenUpdatesAndRenderingCallbacksKeepSessionFences(){
   Owners owner;int replies=0;QVERIFY(owner.ping(replies));QTRY_COMPARE_WITH_TIMEOUT(replies,1,3000);
   ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Vivaldi);QVERIFY(view.beginSession(7));show(view);const auto first=owner.pull();QVERIFY(view.setPresentation(first));
   auto* model=child<QTableView>(view,"azureus-vivaldi-peers-table").model();const auto oldSequence=model->index(0,6).data(Qt::UserRole);view.hide();
   QVERIFY(owner.ping(replies));QTRY_COMPARE_WITH_TIMEOUT(replies,2,3000);const auto later=owner.pull(2);QVERIFY(view.setPresentation(later));QCOMPARE(model->index(0,6).data(Qt::UserRole),oldSequence);
   show(view);QTRY_COMPARE(model->index(0,6).data(Qt::UserRole).toULongLong(),static_cast<qulonglong>(later->snapshot->peerPositions()[0].sequence));
   view.hide();QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(!view.setPresentation(later));show(view);QTRY_COMPARE(model->rowCount(),0);
   QVERIFY(view.setPresentation(owner.pull(1,8)));Hook rebind;rebind.callback=[&]{QVERIFY(view.beginSession(9));QVERIFY(view.setPresentation(owner.pull(1,9)));};
   child<QLabel>(view,"azureus-vivaldi-peers-status").setToolTip({});child<QLabel>(view,"azureus-vivaldi-peers-status").installEventFilter(&rebind);
   QVERIFY(view.setPresentation(owner.pull(2,8)));QVERIFY(!rebind.armed);QTRY_VERIFY(view.presentation()&&view.presentation()->session==9);QTRY_COMPARE(model->rowCount(),1);
   QPointer<ui::AzureusStatisticsView> victim=new ui::AzureusStatisticsView(ui::AzureusStatisticsSection::Vivaldi);QVERIFY(victim->beginSession(7));show(*victim);
   Hook remove;remove.callback=[&]{delete victim.data();};child<QLabel>(*victim,"azureus-vivaldi-peers-status").setToolTip({});child<QLabel>(*victim,"azureus-vivaldi-peers-status").installEventFilter(&remove);
   victim->setPresentation(owner.pull(3));QVERIFY(victim.isNull());QVERIFY(!remove.armed);
 }
 void tooltipCallbackMayDeleteViewOrJustTheStatusChild(){
   Owners owner;QPointer<ui::AzureusStatisticsView> view=new ui::AzureusStatisticsView(ui::AzureusStatisticsSection::Dht);view->beginSession(7);show(*view);child<QLabel>(*view,"azureus-statistics-status").setToolTip({});Hook remove;remove.callback=[&]{delete view.data();};child<QLabel>(*view,"azureus-statistics-status").installEventFilter(&remove);
   view->setPresentation(owner.pull());QVERIFY(!view);QVERIFY(!remove.armed);
   ui::AzureusStatisticsView surviving(ui::AzureusStatisticsSection::Vivaldi);surviving.beginSession(7);show(surviving);QPointer<QLabel> label=&child<QLabel>(surviving,"azureus-statistics-status");label->setToolTip({});Hook removeChild;removeChild.callback=[&]{delete label.data();};label->installEventFilter(&removeChild);QVERIFY(surviving.setPresentation(owner.pull(2)));QVERIFY(!label);QVERIFY(surviving.setPresentation(owner.pull(3)));
   QPointer<ui::AzureusStatisticsView> operations=new ui::AzureusStatisticsView(ui::AzureusStatisticsSection::Operations);QVERIFY(operations->beginSession(7));show(*operations);
   auto& timeline=child<QWidget>(*operations,"azureus-operation-timeline");timeline.setToolTip({});Hook removeOperations;removeOperations.callback=[&]{delete operations.data();};timeline.installEventFilter(&removeOperations);
   operations->setPresentation(owner.pull(4));QVERIFY(operations.isNull());QVERIFY(!removeOperations.armed);
 }
};
QTEST_MAIN(AzureusStatisticsTests)
#include "azureus_statistics_tests.moc"
