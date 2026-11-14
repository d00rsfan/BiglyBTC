// SPDX-License-Identifier: GPL-2.0-or-later
#include "nat_session_fixture.hpp"
#include <QElapsedTimer>
using namespace NatTest;
class AzureusCallRouteTests final:public QObject {
 Q_OBJECT
private slots:
 void originalReachabilityWindowsAndRecentRequestIPs(){using udp::Reachability;

 constexpr long start=1200000;Reachability r(true,start);std::array<unsigned char,4> ip{198,18,1,1},other{198,18,1,2};
 r.sent(ip,start);QVERIFY(!r.received(ip,1030));QVERIFY(r.received(other,1030));QVERIFY(!r.received(other,1030));QVERIFY(r.facts().aliens==1&&r.facts().findValueAliens==1);
 Reachability cold(true,start);for(long n=60000;n<=900000;n+=60000){cold.sample(start+n);}QVERIFY(!cold.facts().accurate&&cold.facts().reachable);QVERIFY(cold.sample(start+960000));QVERIFY(cold.facts().accurate&&!cold.facts().reachable);
 Reachability behind(false,start);for(long minute=1;minute<=17;++minute){for(unsigned n=0;n<32;++n){std::array<unsigned char,4> source{198,19,static_cast<unsigned char>(minute),static_cast<unsigned char>(n)};behind.received(source,1030);}behind.sample(start+minute*60000);}QVERIFY(behind.facts().accurate&&behind.facts().reachable&&behind.facts().findValueAverage>1);for(long minute=18;minute<32;++minute){behind.sample(start+minute*60000);}QVERIFY(!behind.facts().reachable);
 Reachability rotate(true,start);rotate.sent(ip,start);rotate.sent(other,start+180001);std::array<unsigned char,4> third{198,18,2,9};rotate.sent(third,start+360002);QVERIFY(rotate.received(ip,1030));
 Reachability v6(true,start);std::array<unsigned char,16> a{0x20,0x01,0x0d,0xb8};QVERIFY(v6.received(a,1024));QVERIFY(!v6.received(a,1030));QVERIFY(v6.facts().findValueAliens==0);

 }
 void pendingCallMovesOnlyWithItsOwnFreshRoute_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void pendingCallMovesOnlyWithItsOwnFreshRoute(){
  QFETCH(bool,v6);const auto ip=QHostAddress(v6?"::1":"127.121.42.2");Actor client(QHostAddress(v6?"::1":"127.121.42.1"),v6),old(ip,v6),fresh(ip,v6,0,false,-1,old.transport.endpoint().port);admit(client,old);QVERIFY(fresh.transport.allowPeer(client.actualEndpoint));const std::string key(20,'R');unsigned oldRequests=0;std::vector<std::shared_ptr<const udp::TransferRequest>> incoming;
  QVERIFY(client.transport.registerTransferHandler(key,true,&client.receiver,[&](auto r){incoming.push_back(std::move(r));},[&]()->std::optional<std::uint64_t>{return client.revision;}));QVERIFY(old.transport.registerTransferHandler(key,true,&old.receiver,[&](auto){++oldRequests;},[&]()->std::optional<std::uint64_t>{return old.revision;}));QVERIFY(fresh.room(key,[](auto v){return "new route:"+v;}));
  std::optional<udp::TransferOutcome> result;QVERIFY(client.transport.callTransfer(old.actualEndpoint,55,key,"same request",this,[&](auto v){result.emplace(std::move(v));},std::chrono::steady_clock::now()+2s));QTRY_COMPARE_WITH_TIMEOUT(oldRequests,1U,1000);QVERIFY(!result);
  client.sourceHeld=true;auto rejected=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&client.receiver,client.revision);client.sourceHeld=false;QVERIFY(!rejected&&rejected.error()==udp::Failure::Authority);QCOMPARE(fresh.delivered,0U);
  client.sourceHeld=true;auto lease=client.transport.leaseTransferPeer(fresh.actualEndpoint,key,contact(old).exportSnapshot(),&client.receiver,client.revision,std::chrono::steady_clock::now()+30s);QVERIFY(lease);auto wrong=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&old.receiver,client.revision);QVERIFY(!wrong&&wrong.error()==udp::Failure::Authority);auto moved=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&client.receiver,client.revision);client.sourceHeld=false;QVERIFY(moved&&*moved==1);QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1500);QVERIFY(*result&&**result=="new route:same request");QCOMPARE(fresh.delivered,1U);QVERIFY(!client.transport.isRoutingPeer(fresh.actualEndpoint));
  // An already received request is not permission to send its answer to the
  // new endpoint. Withdrawal of its old tuple invalidates that receipt.
  QVERIFY(old.transport.callTransfer(client.actualEndpoint,55,key,"old origin",this,[](auto){},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(!incoming.empty(),1000);const auto receipt=incoming.back()->receipt;client.sourceHeld=true;moved=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&client.receiver,client.revision);client.sourceHeld=false;QVERIFY(moved&&*moved==0);QVERIFY(!client.transport.answerTransfer(receipt,"must not be redirected"));QCOMPARE(fresh.delivered,1U);
  ++client.revision;client.sourceHeld=true;auto stale=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&client.receiver,client.revision);client.sourceHeld=false;QVERIFY(!stale&&stale.error()==udp::Failure::Authority);
 }
 void rerouteDoesNotExtendTheExistingCallDeadline(){
  Actor client(QHostAddress("127.121.43.1"),false),old(QHostAddress("127.121.43.2"),false),fresh(QHostAddress("127.121.43.2"),false,0,false,-1,old.transport.endpoint().port);admit(client,old);QVERIFY(fresh.transport.allowPeer(client.actualEndpoint));const std::string key(20,'D');unsigned seen=0;
  QVERIFY(client.room(key,[](auto v){return v;}));QVERIFY(old.transport.registerTransferHandler(key,true,&old.receiver,[&](auto){++seen;},[&]()->std::optional<std::uint64_t>{return old.revision;}));QVERIFY(fresh.transport.registerTransferHandler(key,true,&fresh.receiver,[](auto){},[&]()->std::optional<std::uint64_t>{return fresh.revision;}));std::optional<udp::TransferOutcome> result;QElapsedTimer time;time.start();QVERIFY(client.transport.callTransfer(old.actualEndpoint,55,key,"finite",this,[&](auto v){result.emplace(std::move(v));},std::chrono::steady_clock::now()+400ms));QTRY_COMPARE_WITH_TIMEOUT(seen,1U,300);
  client.sourceHeld=true;QVERIFY(client.transport.leaseTransferPeer(fresh.actualEndpoint,key,contact(old).exportSnapshot(),&client.receiver,client.revision,std::chrono::steady_clock::now()+30s));auto moved=client.transport.rerouteTransferPeer(key,old.actualEndpoint,fresh.actualEndpoint,&client.receiver,client.revision);client.sourceHeld=false;QVERIFY(moved&&*moved==1);QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1200);QVERIFY(!*result&&result->error()==udp::Failure::Timeout);QVERIFY(time.elapsed()<1200);
 }
 void routabilityIsObservedFromMatchedFindNodeRepliesOnly(){
  Actor a(QHostAddress("127.121.44.1"),false),b(QHostAddress("127.121.44.2"),false);a.sourceHeld=true;QVERIFY(a.transport.attachKnownPeer(b.actualEndpoint,55,std::chrono::steady_clock::now()+3s));a.sourceHeld=false;
  for(const auto status:{std::optional<bool>{true},std::optional<bool>{false},std::optional<bool>{}}){b.sourceHeld=true;QVERIFY(b.transport.setReachability(status));b.sourceHeld=false;std::optional<udp::FindNodeOutcome> result;QVERIFY(a.transport.findNode(b.actualEndpoint,55,contact(a).external().id,this,[&](auto v){result.emplace(std::move(v));},1s));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1500);QVERIFY(*result);const auto* reply=std::get_if<udp::find::ReplyBody>(&(**result).body());QVERIFY(reply);const auto expected=status?(*status?1:0):-1;QCOMPARE(reply->status,expected);auto snapshot=a.transport.routingSnapshot();QVERIFY(snapshot);const auto view=std::span(snapshot->contacts).first(snapshot->contactCount);const auto at=std::ranges::find_if(view,[&](const auto& c){return c.contact.address==contact(b).external().address;});QVERIFY(at!=view.end());QCOMPARE(at->nodeStatus,expected);QCOMPARE(at->protocolSource,udp::RoutingProtocolSource::Packet);}
 }
};
QTEST_GUILESS_MAIN(AzureusCallRouteTests)
#include "azureus_call_route_tests.moc"
