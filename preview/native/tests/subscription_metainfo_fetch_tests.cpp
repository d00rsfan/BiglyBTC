// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_metainfo_fetch.hpp"
#include "subscription_rss_http_fixture.hpp"
#include <QtTest>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <map>

namespace {
namespace s=biglybtc::search;namespace f=subscription_http_fixture;namespace w=rss_fixture;
using Fetch=s::SubscriptionMetainfoFetch;using Phase=s::RssFetchPhase;using Error=s::RssFetchError;
constexpr std::uint64_t Session=271;const QByteArray Key="result-key";
QByteArray metainfo(){
  auto pieces=QByteArray(20,'\0');pieces[1]=static_cast<char>(0xff);
  return f::dictionary({{"info",f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},
      {"piece length",f::integer(16384)},{"pieces",w::b(pieces)}})}});
}
QJsonObject values(const std::string& url){return {{"n","Owned result"},{"lb","1"},{"s","4"},{"p","2"},
    {"tf","10"},{"ts","0"},{"dl",QString::fromStdString(url)}};}
QByteArray install(f::Profile& source,QJsonObject json,std::optional<int> ref={},bool read=false,bool deleted=false){
  auto metadata=biglybtc::core::Document::parse(source.metadataRow.toStdString());w::require(bool(metadata),"Cannot decode real metadata");
  const auto* history=metadata->find("history");w::require(history&&history->type==biglybtc::core::Node::Type::Dictionary,"Actual history missing");
  std::map<QByteArray,QByteArray> fields;
  for(std::size_t i=0;i<history->children.size();i+=2){auto encoded=biglybtc::core::encodeValue(history->children[i+1]);w::require(bool(encoded),"Cannot retain real history field");fields.emplace(QByteArray::fromStdString(std::string(history->children[i].bytes)),QByteArray::fromStdString(*encoded));}
  fields.erase("dl_with_ref");if(ref){fields["dl_with_ref"]=f::integer(*ref);}fields["num_read"]=f::integer(read||deleted?1:0);fields["num_unread"]=f::integer(read||deleted?0:1);
  QByteArray encoded="d";for(const auto& [key,value]:fields){encoded+=w::b(key)+value;}encoded+='e';
  w::require(bool(metadata->replaceValue("history",encoded.toStdString())),"Cannot retain actual local history");
  source.metadataRow=QByteArray::fromStdString(metadata->serialize());source.configuration=f::dictionary({{"subs","l"+source.metadataRow+"e"}});source.restore();
  auto row=f::dictionary({{"key",w::b(Key)},{"read",f::integer(read?1:0)},
      {"result_json",w::b(QJsonDocument(json).toJson(QJsonDocument::Compact))},{"tz",f::integer(10)}});
  if(deleted){auto tombstone=biglybtc::core::Document::parse(row.toStdString());w::require(bool(tombstone)&&bool(tombstone->replaceValue("deleted","i1e")),"Cannot save actual deleted-result flag");row=QByteArray::fromStdString(tombstone->serialize());}
  const auto bytes=f::dictionary({{"future_results",w::b("preserved")},{"results","l"+row+"e"}});w::write(source.resultsPath,bytes);return bytes;
}
auto open(f::Profile& source,s::RssHttpFetch::PrivateEndpointFixture endpoint,std::stop_token stop={}){
  return Fetch::openFixture(*source.lease,source.path.toStdString(),Session,source.selector(),Key.toStdString(),std::move(endpoint),stop);
}
struct Pump {
  Fetch& owner;f::Profile& source;std::optional<s::SubscriptionMetainfoCompletion> completion;std::optional<Error> failure;
  Pump(Fetch& fetch,f::Profile& profile):owner(fetch),source(profile){}
  bool step(){if(completion||failure){return !failure;}auto value=owner.poll(*source.lease,Session);if(!value){failure=value.error();return false;}if(*value){completion.emplace(std::move(**value));}return true;}
};
bool advanceTo(Pump& pump,Phase wanted){
  QElapsedTimer time;time.start();while(time.elapsed()<10000){const auto before=pump.owner.observation();if(before.phase==wanted){return true;}
    if(before.failure){pump.step();return false;}
    if(before.phase==Phase::BeforeDNS||before.phase==Phase::BeforeConnect||before.phase==Phase::BeforeGET){
      if(!pump.step()||pump.completion){return false;}
      while(time.elapsed()<10000){const auto after=pump.owner.observation();if(after.phase!=before.phase||after.hop!=before.hop||after.failure){break;}QTest::qWait(1);}
    }else{QTest::qWait(1);}
  }return false;
}
void closeAndReacquire(std::unique_ptr<Fetch>& owner,f::Profile& source){
  owner->close();w::require(owner->observation().joined,"Metainfo socket/context did not join");source.lease.reset();
  auto acquired=biglybtc::storage::ProfileLease::acquire(source.path.toStdString());w::require(bool(acquired),"Closed metainfo owner retained source/OFD");
  w::require(owner!=nullptr,"Expected closed wrapper to remain alive during lock reacquire");owner.reset();source.lease.emplace(std::move(*acquired));
}
void restore(const std::map<QString,std::optional<QByteArray>>& files){
  for(const auto& [path,bytes]:files){if(bytes){w::write(path,*bytes);}else if(QFile::exists(path)){w::require(QFile::remove(path),"Cannot remove owned fixture source addition");}}
}
}
class SubscriptionMetainfoFetchTests final:public QObject {
 Q_OBJECT
private slots:
 void binaryBytesAndActualDefaultReferer_data(){
   QTest::addColumn<int>("mode");QTest::newRow("fixed-default-referrer")<<0;QTest::newRow("chunked-explicit-no-referrer")<<1;
   QTest::newRow("gzip-default-referrer")<<2;QTest::newRow("TLS-default-referrer")<<3;QTest::newRow("binary-ignores-text-charset")<<4;
 }
 void binaryBytesAndActualDefaultReferer(){
   QFETCH(int,mode);w::Certificate identity;w::Server server(mode==3?&identity:nullptr);const auto original=metainfo();server.body=original;
   if(mode==1){server.chunked=true;}if(mode==2){server.body=w::gzip(original);server.encoding="gzip";}
   if(mode==4){server.rawReply="HTTP/1.1 200 OK\r\nContent-Type: application/x-bittorrent; charset=UTF-16\r\nContent-Length: "+QByteArray::number(original.size())+"\r\n\r\n"+original;}
   auto configured=server.url();if(mode==3){configured.erase(4,1);} // HTTP feed, HTTPS metainfo still needs actual saved JKS.
   f::Profile source(configured,mode==3?&identity.certificate:nullptr);install(source,values(server.url("/owned/item.torrent?literal=1")),mode==1?std::optional<int>{0}:std::nullopt);
   const auto before=source.sourceBytes();auto made=open(source,source.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,source};
   if(mode==1){QVERIFY(advanceTo(pump,Phase::BeforeGET));auto unrelated=biglybtc::storage::ConfigFile::createMissing(*source.lease,"unrelated.config","d6:futurei1ee");QVERIFY(unrelated);}
   QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);QCOMPARE(pump.completion->bytes,original);
   QVERIFY(original.contains('\0')&&original.contains(static_cast<char>(0xff)));QCOMPARE(pump.completion->session,Session);QCOMPARE(pump.completion->gets,std::size_t{1});
   QCOMPARE(server.requests.size(),qsizetype{1});const auto request=server.requests.front();QVERIFY(request.startsWith("GET /owned/item.torrent?literal=1 HTTP/1.1\r\n"));
   const auto feed=configured;const auto slash=feed.find('/',feed.find("://")+3);const auto referer=QByteArray::fromStdString(feed.substr(0,slash));
   if(mode==1){QVERIFY(!request.contains("Referer:"));}else{QVERIFY(request.contains("\r\nReferer: "+referer+"\r\n"));}
   QVERIFY(!request.contains("Cookie:")&&!request.contains("info_hash=")&&!request.startsWith("HEAD "));QVERIFY(source.sourceBytes()==before);
   closeAndReacquire(owner,source);QVERIFY(server.failure.isEmpty());
 }
 void actualGetterPrecedenceControlsTheWire_data(){QTest::addColumn<bool>("magnet");QTest::addColumn<bool>("redirect");QTest::newRow("dbl-precedes-dl")<<false<<false;QTest::newRow("magnet-dbl-prefers-explicit-dl")<<true<<false;QTest::newRow("owned-referrer-survives-redirect")<<false<<true;}
 void actualGetterPrecedenceControlsTheWire(){
   QFETCH(bool,magnet);QFETCH(bool,redirect);w::Server first,second;first.body=metainfo();second.body=metainfo();if(redirect){first.location=QByteArray::fromStdString(second.url("/redirected.torrent"));}f::Profile source(first.url());auto json=values(second.url("/explicit.torrent"));
   json["dbl"]=magnet?QString("MaGnEt:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"):QString::fromStdString(first.url("/button.torrent"));json["pl"]=QString::fromStdString(first.url("/ignored-play"));install(source,json);
   const auto before=source.sourceBytes();auto made=open(source,source.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,source};
   QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);
   QCOMPARE(first.requests.size(),qsizetype{magnet?0:1});QCOMPARE(second.requests.size(),qsizetype{magnet||redirect?1:0});
   QCOMPARE(pump.completion->initialUrl,QUrl(QString::fromStdString(magnet?second.url("/explicit.torrent"):first.url("/button.torrent"))));
   if(redirect){QCOMPARE(pump.completion->finalUrl,QUrl(QString::fromStdString(second.url("/redirected.torrent"))));QCOMPARE(pump.completion->gets,std::size_t{2});}
   const auto feed=first.url();const auto referer=QByteArray::fromStdString(feed.substr(0,feed.find('/',feed.find("://")+3)));
   for(const auto& request:first.requests){QVERIFY(request.contains("\r\nReferer: "+referer+"\r\n"));}for(const auto& request:second.requests){QVERIFY(request.contains("\r\nReferer: "+referer+"\r\n"));}
   QVERIFY(source.sourceBytes()==before);closeAndReacquire(owner,source);QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());
 }
 void actualResultSourceRevokesEveryOwnedPhase_data(){
   QTest::addColumn<int>("mode");QTest::newRow("before-DNS")<<0;QTest::newRow("before-connect")<<1;QTest::newRow("before-GET")<<2;
   QTest::newRow("TLS-before-GET")<<3;QTest::newRow("partial-body")<<4;QTest::newRow("before-delivery")<<5;QTest::newRow("held-resolver")<<6;
 }
 void actualResultSourceRevokesEveryOwnedPhase(){
   QFETCH(int,mode);w::Certificate identity;w::Server server(mode==3?&identity:nullptr);server.body=metainfo();server.holdBody=mode==4;
   f::Profile source(server.url(),mode==3?&identity.certificate:nullptr);const auto stored=install(source,values(server.url("/item.torrent")));const auto before=source.sourceBytes();w::HeldResolver resolver;
   auto made=open(source,mode==6?resolver.endpoint():source.endpoint());QVERIFY(made);auto owner=std::move(*made);const auto nonce=owner->observation().nonce;Pump pump{*owner,source};
   if(mode==1||mode==2||mode==3||mode==5){QVERIFY(advanceTo(pump,mode==1?Phase::BeforeConnect:mode==5?Phase::BeforeDelivery:Phase::BeforeGET));}
   if(mode==4){QVERIFY(advanceTo(pump,Phase::Reading));QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1,10000);}
   if(mode==6){QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);}
   if(mode==0||mode==6){w::write(source.path+"/subscriptions.config",source.configuration+"x");}
   else if(mode==1){w::write(source.path+"/metasearch.config","de");}
   else if(mode==2){w::write(source.path+"/biglybt.config",f::dictionary({{"metasearch.web.last.headers",w::b("WC1UZXN0OiBjaGFuZ2Vk")}}));}
   else if(mode==3){QFile trust(source.path+"/.certs");QVERIFY(trust.open(QIODevice::Append));QCOMPARE(trust.write("x"),qint64{1});trust.close();}
   else{w::write(source.resultsPath,stored+"x");}
   QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source);QVERIFY(!pump.completion);QVERIFY(owner->observation().joined);
   QCOMPARE(server.requests.size(),qsizetype{mode==4||mode==5?1:0});owner->close();owner.reset();server.release();resolver.release();server.holdBody=false;restore(before);
   auto recovery=open(source,source.endpoint());QVERIFY(recovery);owner=std::move(*recovery);Pump fresh{*owner,source};QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);
   QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=nonce);QCOMPARE(fresh.completion->bytes,metainfo());closeAndReacquire(owner,source);QVERIFY(server.failure.isEmpty());
 }
 void receiverCancelSuppressesLateResolverAndFreshNonceRecovers(){
   w::Server server;server.body=metainfo();f::Profile source(server.url());install(source,values(server.url("/item.torrent")));const auto before=source.sourceBytes();
   w::HeldResolver resolver;std::stop_source stop;auto made=open(source,resolver.endpoint(),stop.get_token());QVERIFY(made);auto owner=std::move(*made);const auto nonce=owner->observation().nonce;Pump pump{*owner,source};
   QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);stop.request_stop();QVERIFY(resolver.release()>0);QTRY_VERIFY_WITH_TIMEOUT(resolver.callbacksForwarded.load()==1,10000);
   QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled);QVERIFY(!pump.completion);QCOMPARE(server.connections,0);QVERIFY(owner->observation().joined);QVERIFY(source.sourceBytes()==before);
   owner->close();owner.reset();auto recovery=open(source,source.endpoint());QVERIFY(recovery);owner=std::move(*recovery);Pump fresh{*owner,source};QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);
   QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=nonce);QCOMPARE(fresh.completion->bytes,metainfo());QVERIFY(source.sourceBytes()==before);closeAndReacquire(owner,source);QVERIFY(server.failure.isEmpty());
 }
 void unsupportedSavedResultOrPolicyNeverResolves_data(){
   QTest::addColumn<int>("mode");QTest::newRow("auto-dl")<<0;QTest::newRow("selected-magnet")<<1;QTest::newRow("wrong-result-key")<<2;
   QTest::newRow("read-result")<<3;QTest::newRow("deleted-result")<<4;QTest::newRow("proxy-required")<<5;QTest::newRow("custom-browser-headers")<<6;
 }
 void unsupportedSavedResultOrPolicyNeverResolves(){
   QFETCH(int,mode);w::Server server;f::Profile source(server.url(),nullptr,mode==0);auto json=values(server.url("/item.torrent"));if(mode==1){json["dl"]="magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";}
   install(source,json,{},mode==3,mode==4);if(mode==5){w::write(source.path+"/biglybt.config",f::dictionary({{"metasearch.config.proxy.type",f::integer(1)}}));}
   if(mode==6){w::write(source.path+"/biglybt.config",f::dictionary({{"metasearch.web.last.headers",w::b("Q29va2llOiBzZWNyZXQ=")}}));}
   const auto before=source.sourceBytes();std::atomic<int> submitted{0};auto endpoint=source.endpoint();auto original=std::move(endpoint.resolve);
   endpoint.resolve=[&](const QString& host,QObject* context,s::RssHttpFetch::PrivateEndpointFixture::Completion completion){++submitted;return original(host,context,std::move(completion));};
   auto made=Fetch::openFixture(*source.lease,source.path.toStdString(),Session,source.selector(),mode==2?std::string(10,'x'):Key.toStdString(),std::move(endpoint));
   QVERIFY(!made);QCOMPARE(submitted.load(),0);QCOMPARE(server.connections,0);QVERIFY(source.sourceBytes()==before);source.lease.reset();QVERIFY(biglybtc::storage::ProfileLease::acquire(source.path.toStdString()));
 }
 void boundedWireFailuresProduceNoMetainfoCompletion_data(){
   QTest::addColumn<int>("mode");QTest::newRow("redirect-limit")<<0;QTest::newRow("HTTP500")<<1;QTest::newRow("untrusted-TLS")<<2;QTest::newRow("declared-over4MiB")<<3;
 }
 void boundedWireFailuresProduceNoMetainfoCompletion(){
   QFETCH(int,mode);w::Certificate identity;w::Server server(mode==2?&identity:nullptr);server.body=metainfo();server.redirectEachRequest=mode==0;
   if(mode==1){server.rawReply="HTTP/1.1 500 Failed\r\nContent-Length: 0\r\n\r\n";}if(mode==3){server.rawReply="HTTP/1.1 200 OK\r\nContent-Length: 4194305\r\n\r\n";}
   f::Profile source(server.url());install(source,values(server.url("/item.torrent")));const auto before=source.sourceBytes();auto made=open(source,source.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,source};
   QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(pump.failure&&!pump.completion);QCOMPARE(*pump.failure,mode==0?Error::Limit:mode==2?Error::Tls:Error::Http);
   const auto first=*pump.failure;owner->requestCancel();const auto again=owner->poll(*source.lease,Session);QVERIFY(!again);QCOMPARE(again.error(),first);
   QCOMPARE(server.requests.size(),qsizetype{mode==0?6:mode==2?0:1});QVERIFY(owner->observation().joined);QVERIFY(source.sourceBytes()==before);owner->close();QVERIFY(owner->observation().failure&&*owner->observation().failure==first);closeAndReacquire(owner,source);
 }
};
QTEST_GUILESS_MAIN(SubscriptionMetainfoFetchTests)
#include "subscription_metainfo_fetch_tests.moc"
