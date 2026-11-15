// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
using namespace generated_cache;
namespace {
udp::query::RequestBody request(const std::string& key){udp::query::RequestBody body;body.groups={{key.substr(0,3),{key.substr(3,3)}},{"",{std::string(6,'Z'),key.substr(0,6)}}};return body;}
struct Exchange {std::optional<udp::QueryStoreOutcome> result;};
bool send(Fixture& f,QObject* receiver,Exchange& exchange,const udp::query::RequestBody& body,std::chrono::milliseconds timeout=1500ms){bool accepted=false;f.b.grant([&]{accepted=bool(f.b.transport.queryStorage(f.a.transport.endpoint(),55,body,receiver,[&exchange](auto result){exchange.result.emplace(std::move(result));},timeout));});return accepted;}
const udp::query::ReplyBody* reply(Exchange& exchange){if(!exchange.result||!*exchange.result){return nullptr;}const auto* packet=std::get_if<udp::query::Decoded>(&**exchange.result);return packet?std::get_if<udp::query::ReplyBody>(&packet->body()):nullptr;}
}
class AzureusQueryNetworkTests final:public QObject {Q_OBJECT
private slots:
 void actualSeededActorQueriesBothAddressFamilies_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void actualSeededActorQueriesBothAddressFamilies(){
  QFETCH(bool,ipv6);Fixture f(20,ipv6);Clock clock;QVERIFY(f.a.database->putLocal(f.key,"generated query payload",1,clock));const auto reads=f.a.database->snapshot()->reads;Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key)));QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),1800);auto response=reply(exchange);QVERIFY(response);QCOMPARE(response->headerLength,std::uint8_t{2});QCOMPARE(response->entries.size(),std::size_t{3});QCOMPARE(response->entries[0],std::optional<std::string>(f.key.substr(18,2)));QVERIFY(!response->entries[1]);QCOMPARE(response->entries[2],response->entries[0]);QCOMPARE(f.a.database->snapshot()->reads,reads);QCOMPARE(f.a.database->snapshot()->localPuts,std::uint64_t{1});QCOMPARE(f.a.database->snapshot()->stores,std::uint64_t{0});
 }
 void queryDoesNotGrantAStoreToken(){
  Fixture f;Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key)));QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),1800);QVERIFY(reply(exchange));udp::StoreRequest value;value.keys={f.key};value.valueSets={{{1,QDateTime::currentMSecsSinceEpoch(),"without token",f.b.local,0,1,-1}}};std::optional<udp::StoreOutcome> result;f.b.grant([&]{QVERIFY(f.b.transport.store(f.a.transport.endpoint(),55,std::move(value),this,[&](auto out){result.emplace(std::move(out));},500ms));});QTest::qWait(80);QVERIFY(!f.a.database->contains(f.key).value());QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),700);
 }
 void heldSourceDoesNotReplyAndWithdrawalCannotLeakAKey(){
  Fixture f;Clock clock;QVERIFY(f.a.database->putLocal(f.key,"owned",1,clock));f.a.hold=true;Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key)));QTRY_VERIFY_WITH_TIMEOUT(!f.a.pending.empty(),500);QVERIFY(!exchange.result);auto source=std::move(f.a.pending.front());f.a.pending.clear();f.a.current=false;f.a.grant([&]{source(true);});QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),1800);QVERIFY(!*exchange.result);QCOMPARE(f.a.database->snapshot()->reads,std::uint64_t{0});
 }
 void keyMutationDuringReadPermitCannotReturnAnEarlierIndexResult(){
  Fixture f;Clock clock;QVERIFY(f.a.database->putLocal(f.key,"owned",1,clock));unsigned calls=0;f.a.keyGate=[&](std::string key,auto done){++calls;need(key==f.key,"Generated current query key");f.a.grant([&]{QVERIFY(f.a.database->removeLocal(key,2,clock));done(true);});return true;};Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key),500ms));QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),800);QVERIFY(!*exchange.result);QCOMPARE(calls,1u);QCOMPARE(f.a.database->snapshot()->reads,std::uint64_t{0});
 }
 void aLateRejectedRegistrationCannotSendOrAffectANewerQuery(){
  Fixture f;std::move_only_function<void(bool)> late;f.a.normalGate=[&](auto done){late=std::move(done);return false;};Exchange first;QVERIFY(send(f,this,first,request(f.key),500ms));QTRY_VERIFY_WITH_TIMEOUT(bool(late),500);f.a.normalGate={};Exchange second;QVERIFY(send(f,this,second,request(f.key)));QTRY_VERIFY_WITH_TIMEOUT(second.result.has_value(),1800);QVERIFY(reply(second));const auto before=f.a.transport.statisticsSnapshot()->counters().sent;f.a.grant([&]{late(true);});QTest::qWait(10);QCOMPARE(f.a.transport.statisticsSnapshot()->counters().sent,before);QTRY_VERIFY_WITH_TIMEOUT(first.result.has_value(),700);QVERIFY(!*first.result);
 }
 void callbackAfterCloseIsInert(){
  Fixture f;f.a.hold=true;Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key),500ms));QTRY_VERIFY_WITH_TIMEOUT(!f.a.pending.empty(),500);auto late=std::move(f.a.pending.front());f.a.pending.clear();f.a.transport.close();f.a.grant([&]{late(true);});QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),700);QVERIFY(!*exchange.result);
 }
 void foreignReplyCannotCompleteAnActualTransaction(){
  Fixture f;f.a.hold=true;Exchange exchange;QVERIFY(send(f,this,exchange,request(f.key),500ms));QTRY_VERIFY_WITH_TIMEOUT(!f.a.pending.empty(),500);udp::query::ReplyFields fake;fake.header.network=4;fake.header.connection=INT64_MIN;fake.header.transaction=17;auto wire=udp::query::encodeReply(fake);QVERIFY(wire);QUdpSocket stranger;QVERIFY(stranger.bind(QHostAddress("127.144.71.1"),0));QCOMPARE(stranger.writeDatagram(wire->data(),qint64(wire->size()),f.b.transport.endpoint().address,f.b.transport.endpoint().port),qint64(wire->size()));QTest::qWait(10);QVERIFY(!exchange.result);auto source=std::move(f.a.pending.front());f.a.pending.clear();f.a.hold=false;f.a.grant([&]{source(true);});QTRY_VERIFY_WITH_TIMEOUT(exchange.result.has_value(),700);QVERIFY(reply(exchange));
 }
 void oversizedCompressedQueryCannotAmplifyAResponse(){
  Fixture f;udp::query::RequestBody body;body.groups={{f.key.substr(0,6),std::vector<std::string>(385)}};bool accepted=true;f.b.grant([&]{accepted=bool(f.b.transport.queryStorage(f.a.transport.endpoint(),55,body,this,[](auto){},100ms));});QVERIFY(!accepted);
 }
};
QTEST_GUILESS_MAIN(AzureusQueryNetworkTests)
#include "azureus_query_network_tests.moc"
