// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_automatic_fixture.hpp"
#include <QtTest>
#include <QElapsedTimer>
namespace {
namespace a=subscription_automatic_fixture;namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=biglybtc::search;
using Fetch=s::SubscriptionMetainfoFetch;using Phase=s::RssFetchPhase;using Error=s::RssFetchError;
constexpr std::uint64_t Session=301;
QByteArray binary(){return QByteArray("d4:info",7)+QByteArray(1,'\0')+QByteArray(1,static_cast<char>(0xff))+"e";}
void result(a::Profile& area,const std::string& url){area.result({{"n","Owned"},{"lb","1"},{"s","4"},{"p","2"},{"dl",QString::fromStdString(url)}});}
auto open(a::Profile& area,s::RssHttpFetch::PrivateEndpointFixture endpoint,std::stop_token stop={}){
 return Fetch::openAutomaticFixture(*area.lease,area.path.toStdString(),Session,area.selector(),"result-key",std::move(endpoint),stop);
}
struct Pump {
 Fetch& owner;a::Profile& area;std::optional<s::SubscriptionMetainfoCompletion> completion;std::optional<Error> failure;
 Pump(Fetch& value,a::Profile& source):owner(value),area(source){}
 bool step(){if(completion||failure){return !failure;}auto value=owner.poll(*area.lease,Session);if(!value){failure=value.error();return false;}if(*value){completion.emplace(std::move(**value));}return true;}
};
bool advanceTo(Pump& pump,Phase wanted){
 QElapsedTimer timer;timer.start();while(timer.elapsed()<10000){const auto before=pump.owner.observation();if(before.phase==wanted){return true;}if(before.failure){pump.step();return false;}
  if(before.phase==Phase::BeforeDNS||before.phase==Phase::BeforeConnect||before.phase==Phase::BeforeGET){
   if(!pump.step()||pump.completion){return false;}while(timer.elapsed()<10000){const auto after=pump.owner.observation();if(after.phase!=before.phase||after.hop!=before.hop||after.failure){break;}QTest::qWait(1);}
  }else{QTest::qWait(1);}
 }return false;
}
void joined(std::unique_ptr<Fetch>& owner,a::Profile& area){
 owner->close();w::require(owner->observation().joined,"Automatic metainfo context did not join");area.lease.reset();auto acquired=biglybtc::storage::ProfileLease::acquire(area.path.toStdString());w::require(bool(acquired),"Closed automatic owner retained actual source/OFD");owner.reset();area.lease.emplace(std::move(*acquired));
}
}
class SubscriptionAutomaticMetainfoTests final:public QObject {
 Q_OBJECT
private slots:
 void actualAutomaticModeDeliversOnlyOwnedBinary_data(){QTest::addColumn<bool>("tls");QTest::newRow("plain-binary")<<false;QTest::newRow("actual-JKS-TLS")<<true;}
 void actualAutomaticModeDeliversOnlyOwnedBinary(){
  QFETCH(bool,tls);w::Certificate certificate;w::Server server(tls?&certificate:nullptr);server.body=binary();a::Profile area(server.url(),{},tls?&certificate.certificate:nullptr);result(area,server.url("/owned.torrent?literal=1"));const auto before=area.sourceBytes();
  auto manual=Fetch::openFixture(*area.lease,area.path.toStdString(),Session,area.selector(),"result-key",area.endpoint());QVERIFY(!manual);QCOMPARE(server.connections,0);
  auto made=open(area,area.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,area};QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);QCOMPARE(pump.completion->bytes,binary());QCOMPARE(pump.completion->session,Session);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.requests[0].startsWith("GET /owned.torrent?literal=1 HTTP/1.1\r\n"));QVERIFY(!server.requests[0].contains("info_hash="));QVERIFY(area.sourceBytes()==before);joined(owner,area);QVERIFY(server.failure.isEmpty());
 }
 void actualPolicyRevokesAutomaticPhases_data(){QTest::addColumn<int>("phase");QTest::newRow("before-DNS")<<0;QTest::newRow("before-GET")<<1;QTest::newRow("reading")<<2;QTest::newRow("before-delivery")<<3;}
 void actualPolicyRevokesAutomaticPhases(){
  QFETCH(int,phase);w::Server server;server.body=binary();server.holdBody=phase==2;a::Profile area(server.url());result(area,server.url("/owned.torrent"));const auto config=a::bytes(area.path+"/subscriptions.config"),rows=a::bytes(area.resultsPath);
  auto made=open(area,area.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,area};if(phase){QVERIFY(advanceTo(pump,phase==1?Phase::BeforeGET:phase==2?Phase::Reading:Phase::BeforeDelivery));}if(phase==2){QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1,10000);}
  area.setting("Default Start Torrents Stopped","i1e");QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source&&!pump.completion);QCOMPARE(server.requests.size(),qsizetype{phase>=2?1:0});QCOMPARE(a::bytes(area.path+"/subscriptions.config"),config);QCOMPARE(a::bytes(area.resultsPath),rows);joined(owner,area);
 }
 void receiverCancelDiscardsLateResolverAndFreshNonceRecovers(){
  w::Server server;server.body=binary();a::Profile area(server.url());result(area,server.url("/owned.torrent"));const auto before=area.sourceBytes();w::HeldResolver held;std::stop_source stop;auto made=open(area,held.endpoint(),stop.get_token());QVERIFY(made);auto owner=std::move(*made);const auto nonce=owner->observation().nonce;Pump pump{*owner,area};
  QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(held.count()==1,1000);stop.request_stop();QVERIFY(held.release()>0);QTRY_VERIFY_WITH_TIMEOUT(held.callbacksForwarded.load()==1,10000);QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled&&!pump.completion);QCOMPARE(server.connections,0);QVERIFY(owner->observation().joined);owner->close();owner.reset();
  auto recovery=open(area,area.endpoint());QVERIFY(recovery);owner=std::move(*recovery);Pump fresh{*owner,area};QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=nonce);QCOMPARE(fresh.completion->bytes,binary());QVERIFY(area.sourceBytes()==before);joined(owner,area);
 }
 void automaticFactoryCannotConsumeManualSource(){
  w::Server server;f::Profile area(server.url());w::write(area.resultsPath,f::dictionary({{"results","l"+f::dictionary({{"key",w::b("result-key")},{"read",f::integer(0)},{"result_json",w::b("{\"n\":\"Owned\",\"dl\":\"http://files.example.org/item.torrent\"}")}})+"e"}}));const auto before=area.sourceBytes();std::atomic<int> submissions{0};auto endpoint=area.endpoint();auto resolve=std::move(endpoint.resolve);endpoint.resolve=[&](const QString& host,QObject* context,s::RssHttpFetch::PrivateEndpointFixture::Completion completion){++submissions;return resolve(host,context,std::move(completion));};
  auto made=Fetch::openAutomaticFixture(*area.lease,area.path.toStdString(),Session,area.selector(),"result-key",std::move(endpoint));QVERIFY(!made);QCOMPARE(submissions.load(),0);QCOMPARE(server.connections,0);QVERIFY(area.sourceBytes()==before);
 }
};
QTEST_GUILESS_MAIN(SubscriptionAutomaticMetainfoTests)
#include "subscription_automatic_metainfo_tests.moc"
