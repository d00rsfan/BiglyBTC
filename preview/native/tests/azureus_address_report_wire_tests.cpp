// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include <QNetworkDatagram>
#include <type_traits>
using namespace generated_cache;
static_assert(!std::is_default_constructible_v<udp::AddressReport>);
class AzureusAddressReportWireTests final:public QObject {Q_OBJECT
 struct Exchange {
  Actor actor;QUdpSocket reporter,foreign;QObject receiver;
  struct BeforeDelivery final:QObject {
   std::function<void()> effect;
   bool eventFilter(QObject*,QEvent* event)override {if(event->type()==QEvent::MetaCall&&effect){auto run=std::exchange(effect,{});run();}return false;}
  } beforeDelivery;
  std::vector<udp::AddressReport> reports;std::optional<udp::DetailedFindNodeOutcome> result;
  std::optional<udp::find::Decoded> request;udp::Peer remote;
  explicit Exchange(bool v6):actor(v6?"::1":"127.151.73.1",20){
   const QHostAddress bind(v6?"::1":"127.152.73.1");need(reporter.bind(bind,0)&&foreign.bind(bind,0),"Generated report sockets");remote={reporter.localAddress(),reporter.localPort()};
   actor.grant([&]{need(bool(actor.transport.allowPeer(remote)),"Generated report peer hint");});
   need(bool(actor.transport.setAddressReportSink([this](const auto& report){reports.push_back(report);})),"Generated report observer");
  }
  void start(std::chrono::milliseconds duration=500ms){
   actor.grant([&]{need(bool(actor.transport.requestStoreToken(remote,55,&receiver,[this](auto r){result.emplace(std::move(r));},duration)),"Generated report actual request");});
   need(spin([&]{return reporter.hasPendingDatagrams();}),"Generated request arrived");auto datagram=reporter.receiveDatagram();udp::find::Context context;context.header.network=actor.local.address.length==16?3:4;context.header.localVersion=55;context.header.now=QDateTime::currentMSecsSinceEpoch();nodes::NodeCache cache;
   auto decoded=udp::find::decode(udp::packet::udp::Kind::Request,std::string(datagram.data().constData(),datagram.data().size()),context,cache);need(bool(decoded),"Generated actual request decode");request=std::move(*decoded);
  }
  std::string reply(int type=1,int transactionDelta=0,int connectionDelta=0,int networkDelta=0){
   const auto& requestHeader=request->header().fields();udp::remote_error::ReplyFields fields;fields.type=type;auto& h=fields.header;h.network=requestHeader.network+networkDelta;h.localVersion=55;h.remoteVersion=55;h.transaction=requestHeader.transaction+transactionDelta;h.connection=requestHeader.connection+connectionDelta;
   // The documentation address is an untrusted remote claim, never a bound socket.
   auto claim=contact({QHostAddress(actor.local.address.length==16?"2001:db8::123":"203.0.113.123"),43123}).address;
   if(type==1){fields.originator=udp::remote_error::Endpoint{claim};}
   auto encoded=udp::remote_error::encodeReply(fields);need(bool(encoded),"Generated remote error encode");return std::move(*encoded);
  }
  void send(const std::string& bytes,bool wrongPort=false){auto& socket=wrongPort?foreign:reporter;const auto local=actor.transport.endpoint();need(socket.writeDatagram(bytes.data(),qint64(bytes.size()),local.address,local.port)==qint64(bytes.size()),"Generated report datagram sent");}
 };
private slots:
 void actualCorrelatedIpv4AndIpv6_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("ipv4")<<false;QTest::newRow("ipv6")<<true;}
 void actualCorrelatedIpv4AndIpv6(){QFETCH(bool,ipv6);Exchange f(ipv6);f.start();f.send(f.reply());QTRY_COMPARE_WITH_TIMEOUT(f.reports.size(),std::size_t{1},1000);QTRY_VERIFY_WITH_TIMEOUT(f.result,1000);QVERIFY(*f.result);QVERIFY(std::get_if<udp::remote_error::Decoded>(&**f.result));const auto& r=f.reports.front();QCOMPARE(r.reporter(),f.remote);QCOMPARE(r.reported().port,quint16{43123});QCOMPARE(r.reported().length,ipv6?16:4);QVERIFY(r.generation());QCOMPARE(r.sequence(),std::uint64_t{1});QCOMPARE(r.observedContacts(),std::size_t{0});}
 void unrelatedPacketsNeverBecomeClaims(){Exchange f(false);f.start();f.send(f.reply(1,1));f.send(f.reply(1,0,1));f.send(f.reply(1,0,0,1));f.send(f.reply(),true);QTest::qWait(20);QVERIFY(f.reports.empty());QVERIFY(!f.result);f.send(f.reply());QTRY_COMPARE_WITH_TIMEOUT(f.reports.size(),std::size_t{1},1000);}
 void nonAddressErrorNeverBecomesClaim(){Exchange f(false);f.start();f.send(f.reply(0));QTRY_VERIFY_WITH_TIMEOUT(f.result,1000);QVERIFY(*f.result);QVERIFY(f.reports.empty());}
 void expiredRpcNeverBecomesClaim(){Exchange f(false);f.start(30ms);QTRY_VERIFY_WITH_TIMEOUT(f.result,500);QVERIFY(!*f.result);f.send(f.reply());QTest::qWait(20);QVERIFY(f.reports.empty());}
 void clearingSinkRevokesQueuedClaim(){Exchange f(false);f.start();f.send(f.reply());f.beforeDelivery.effect=[&]{need(f.actor.transport.pendingCount()==0,"Generated claim queued after correlation");need(bool(f.actor.transport.setAddressReportSink({})),"Generated sink revoked");};f.actor.transport.installEventFilter(&f.beforeDelivery);QTRY_VERIFY_WITH_TIMEOUT(f.result,1000);QVERIFY(!f.beforeDelivery.effect);QVERIFY(f.reports.empty());}
 void closeRevokesQueuedClaim(){Exchange f(false);f.start();f.send(f.reply());f.beforeDelivery.effect=[&]{need(f.actor.transport.pendingCount()==0,"Generated claim queued after correlation");f.actor.transport.close();};f.actor.transport.installEventFilter(&f.beforeDelivery);QTRY_VERIFY_WITH_TIMEOUT(f.result,1000);QVERIFY(!f.beforeDelivery.effect);QVERIFY(f.reports.empty());}
};
QTEST_GUILESS_MAIN(AzureusAddressReportWireTests)
#include "azureus_address_report_wire_tests.moc"
