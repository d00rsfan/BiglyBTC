// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_fixture.hpp"
#include "metasearch_metainfo_fetch.hpp"
#include "../services/http/http_response.hpp"
#include <QtTest>
#include <QElapsedTimer>
#include <QDebug>
#include <QJsonArray>
#include <stop_token>
namespace {
namespace s=biglybtc::search;namespace f=metasearch_fixture;namespace w=rss_fixture;
using Query=s::MetasearchHttpFetch;using Binary=s::MetasearchMetainfoFetch;
using Phase=s::RssFetchPhase;using Error=s::RssFetchError;
constexpr std::uint64_t Session=91;
const std::string Text="needle μ +*/~";
QByteArray json(const QString& download={}){QJsonObject row{{"title","Owned"},{"size",qint64{1}},{"seeds",qint64{2}},{"peers",qint64{3}}};if(!download.isEmpty()){row.insert("download",download);}return QJsonDocument(QJsonObject{{"rows",QJsonArray{row}}}).toJson(QJsonDocument::Compact);}
QByteArray metainfo(){const auto info=f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},{"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}});return f::dictionary({{"info",info}});}
template<class Fetch,class Completion>struct Pump {
  Fetch& owner;f::Profile& profile;std::optional<Completion> completion;std::optional<Error> failure;
  Pump(Fetch& value,f::Profile& source):owner(value),profile(source){}
  bool step(){if(completion||failure){return !failure;}auto value=owner.poll(*profile.lease,Session);if(!value){failure=value.error();return false;}if(*value){completion.emplace(std::move(**value));}return true;}
};
using QueryPump=Pump<Query,s::MetasearchFetchCompletion>;using BinaryPump=Pump<Binary,s::MetasearchMetainfoCompletion>;
template<class P>bool advanceTo(P& pump,Phase wanted){
  QElapsedTimer time;time.start();while(time.elapsed()<10000){const auto before=pump.owner.observation();if(before.phase==wanted){return true;}if(before.failure){pump.step();return false;}
    if(before.phase==Phase::BeforeDNS||before.phase==Phase::BeforeConnect||before.phase==Phase::BeforeGET){if(!pump.step()||pump.completion){return false;}while(time.elapsed()<10000){const auto after=pump.owner.observation();if(after.phase!=before.phase||after.hop!=before.hop||after.failure){break;}QTest::qWait(1);}}
    else{QTest::qWait(1);}
  }return false;
}
auto query(f::Profile& profile,s::RssHttpFetch::PrivateEndpointFixture endpoint,std::stop_token stop={}){
  auto value=Query::openFixture(*profile.lease,profile.path.toStdString(),Session,f::Profile::selector(),Text,std::move(endpoint),stop);
  if(!value){const auto source=s::SavedMetasearchSourceSnapshot::read(*profile.lease,profile.path.toStdString(),Session,f::Profile::selector(),Text);qWarning().noquote()<<"METASEARCH_OPEN_FAILURE fetch="<<static_cast<int>(value.error())<<"source="<<(source?-1:static_cast<int>(source.error()));}return value;
}
template<class F>void closeAndRelease(std::unique_ptr<F>& owner,f::Profile& profile){
  owner->close();w::require(owner->observation().joined,"Metasearch event loop did not join");profile.lease.reset();auto lock=biglybtc::storage::ProfileLease::acquire(profile.path.toStdString());w::require(bool(lock),"Kept-alive closed transport retained profile alias");profile.lease.emplace(std::move(*lock));owner.reset();
}
} // namespace
class MetasearchTransportTests final:public QObject {
  Q_OBJECT
private slots:
  void actualQueryGetEncodingAndConfiguredContext_data(){QTest::addColumn<int>("mode");QTest::newRow("json-no-path-base")<<0;QTest::newRow("rss-chunked")<<1;QTest::newRow("json-gzip-preserves-existing-escapes")<<2;QTest::newRow("redirect-keeps-configured-context")<<3;}
  void actualQueryGetEncodingAndConfiguredContext(){
    QFETCH(int,mode);w::Server server,final;const auto plain=mode==1?w::rss():json();server.body=plain;if(mode==1){server.chunked=true;}if(mode==2){server.encoding="gzip";server.body=w::gzip(plain);}if(mode==3){server.location=QByteArray::fromStdString(final.url("/delivered/response.json"));final.body=plain;}
    const auto configured=server.url(mode==0?"?q=%s&fixed=1":mode==2?"/%41/search?q=%s&fixed=%7E&reserved=%2f":"/configured/search?q=%s&fixed=1");f::Profile profile(configured);if(mode==1){profile.setRows(f::engine(server.url("/configured/search?q=%s&fixed=1"),7,"owned-json",4));}const auto saved=profile.primary;auto opened=query(profile,profile.endpoint());QVERIFY(opened);auto owner=std::move(*opened);QueryPump pump{*owner,profile};QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);
    const auto& result=*pump.completion;QCOMPARE(result.bytes,plain);QCOMPARE(result.session,Session);QVERIFY(result.nonce>0);QCOMPARE(result.context.rootPage,mode==0?QString::fromStdString(configured):QStringLiteral("http://feeds.example.org:")+QString::number(server.serverPort()));QCOMPARE(result.context.basePage,mode==0?QString{}:result.context.rootPage+(mode==2?"/%41/":"/configured/"));
    QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.requests.front().startsWith(mode==0?"GET /?q=needle+%CE%BC+%2B*%2F%7E&fixed=1 HTTP/1.1\r\n":mode==2?"GET /%41/search?q=needle+%CE%BC+%2B*%2F%7E&fixed=%7E&reserved=%2f HTTP/1.1\r\n":"GET /configured/search?q=needle+%CE%BC+%2B*%2F%7E&fixed=1 HTTP/1.1\r\n"));QVERIFY(!server.requests.front().contains("info_hash="));QVERIFY(server.requests.front().contains("\r\nReferer: "+result.context.rootPage.toUtf8()+"\r\n"));QVERIFY(server.failure.isEmpty());
    QCOMPARE(result.gets,std::size_t{mode==3?2U:1U});if(mode==3){QCOMPARE(result.finalUrl,QUrl(QString::fromStdString(final.url("/delivered/response.json"))));QCOMPARE(final.requests.size(),qsizetype{1});QVERIFY(final.requests.front().startsWith("GET /delivered/response.json HTTP/1.1\r\n"));}else{QCOMPARE(result.finalUrl,result.initialUrl);}QCOMPARE(f::bytes(profile.path+"/metasearch.config"),saved);QVERIFY(!owner->observation().failure);owner->close();QVERIFY(!owner->observation().failure);closeAndRelease(owner,profile);
  }
  void actualTlsTrustAndHostnameBeforeQueryGet_data(){QTest::addColumn<int>("mode");QTest::newRow("actual-saved-root")<<0;QTest::newRow("unknown-root-no-get")<<1;QTest::newRow("wrong-hostname-no-get")<<2;}
  void actualTlsTrustAndHostnameBeforeQueryGet(){
    QFETCH(int,mode);w::Certificate certificate(mode==2?"wrong.example.org":"feeds.example.org"),unrelated;w::Server server(&certificate);server.body=json();f::Profile profile(server.url("/query?q=%s"),mode==1?&unrelated.certificate:&certificate.certificate);auto opened=query(profile,profile.endpoint());QVERIFY(opened);auto owner=std::move(*opened);QueryPump pump{*owner,profile};QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);
    if(mode==0){QVERIFY(!pump.failure&&pump.completion);QCOMPARE(server.requests.size(),qsizetype{1});}else{QVERIFY(pump.failure&&*pump.failure==Error::Tls&&!pump.completion);QCOMPARE(server.requests.size(),qsizetype{0});QVERIFY(server.connections>0);}closeAndRelease(owner,profile);
  }
  void actualSourceRevocationAtOwnedPhases_data(){QTest::addColumn<int>("mode");QTest::newRow("before-dns")<<0;QTest::newRow("before-get-after-tls")<<1;QTest::newRow("held-body")<<2;QTest::newRow("before-delivery")<<3;}
  void actualSourceRevocationAtOwnedPhases(){
    QFETCH(int,mode);w::Certificate certificate;w::Server server(&certificate);server.body=json();server.holdBody=mode==2;f::Profile profile(server.url("/query?q=%s"),&certificate.certificate);auto opened=query(profile,profile.endpoint());QVERIFY(opened);auto owner=std::move(*opened);QueryPump pump{*owner,profile};const auto wanted=mode==0?Phase::BeforeDNS:mode==1?Phase::BeforeGET:mode==2?Phase::Reading:Phase::BeforeDelivery;QVERIFY(advanceTo(pump,wanted));if(mode==1){QTRY_VERIFY_WITH_TIMEOUT(server.encrypted>0,10000);QCOMPARE(owner->observation().phase,Phase::BeforeGET);QCOMPARE(server.requests.size(),qsizetype{0});}if(mode==2){QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1,10000);}
    w::write(profile.path+"/biglybt.config",f::dictionary({{"source-revoked",f::integer(1)}}));QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source&&!pump.completion);QVERIFY(owner->observation().joined);if(mode==0){QCOMPARE(owner->observation().resolverSubmissions,std::size_t{0});}owner->close();server.release();w::write(profile.path+"/biglybt.config","de");const auto oldNonce=owner->observation().nonce;owner.reset();
    auto fresh=query(profile,profile.endpoint());QVERIFY(fresh);owner=std::move(*fresh);QVERIFY(owner->observation().nonce>oldNonce);QueryPump recovery{*owner,profile};QTRY_VERIFY_WITH_TIMEOUT(!recovery.step()||recovery.completion,10000);QVERIFY(!recovery.failure&&recovery.completion);closeAndRelease(owner,profile);
  }
  void receiverCancellationDiscardsLateDnsAndFreshQueryRecovers(){
    w::Server server;server.body=json();f::Profile profile(server.url("/query?q=%s"));w::HeldResolver resolver;std::stop_source receiver;auto opened=query(profile,resolver.endpoint(),receiver.get_token());QVERIFY(opened);auto owner=std::move(*opened);QueryPump pump{*owner,profile};QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);receiver.request_stop();QVERIFY(resolver.release()>0);QTRY_VERIFY_WITH_TIMEOUT(resolver.callbacksForwarded.load()==1,10000);QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled&&!pump.completion);QCOMPARE(server.requests.size(),qsizetype{0});const auto nonce=owner->observation().nonce;closeAndRelease(owner,profile);
    auto fresh=query(profile,profile.endpoint());QVERIFY(fresh);owner=std::move(*fresh);QVERIFY(owner->observation().nonce>nonce);QueryPump recovery{*owner,profile};QTRY_VERIFY_WITH_TIMEOUT(!recovery.step()||recovery.completion,10000);QVERIFY(!recovery.failure&&recovery.completion);closeAndRelease(owner,profile);
  }
  void actualCompletedResultHandleOwnsBinaryAndQueryLifetime_data(){QTest::addColumn<int>("mode");QTest::newRow("actual-completed-key-to-binary")<<0;QTest::newRow("query-cancel-before-binary-dns")<<1;QTest::newRow("new-query-revokes-held-binary")<<2;}
  void actualCompletedResultHandleOwnsBinaryAndQueryLifetime(){
    QFETCH(int,mode);w::Server server;const auto resultUrl=QString::fromStdString(server.url("/owned/result.torrent"));server.body=json(resultUrl);f::Profile profile(server.url("/query?q=%s"));const auto saved=profile.primary;int made=0;auto serviceValue=s::SavedMetasearchService::open(*profile.lease,profile.path.toStdString(),Session,s::MetasearchServiceMode::Normal,f::factory(profile,made));QVERIFY(serviceValue);auto service=std::move(*serviceValue);std::stop_source receiver;auto begun=service->beginSearch(*profile.lease,Session,{f::Profile::selector()},Text,receiver.get_token());QVERIFY(begun);std::vector<s::MetasearchSearchUpdate> updates;
    QTRY_VERIFY_WITH_TIMEOUT((f::pump(*service,*profile.lease,Session,updates),!updates.empty()&&updates.back().done),10000);QVERIFY2(!updates.back().hadFailures&&!updates.back().engine.failure,qPrintable(QString("made=%1 failure=%2 diagnostic=%3").arg(made).arg(updates.back().engine.failure?static_cast<int>(*updates.back().engine.failure):-1).arg(QString::fromStdString(updates.back().engine.diagnostic).left(512))));QCOMPARE(updates.back().engine.rows.size(),std::size_t{1});const auto key=updates.back().engine.rows.front().key;QCOMPARE(key.queryNonce,*begun);QVERIFY(key.rowId>0);auto retained=service->retainResult(*profile.lease,Session,key);QVERIFY(retained);std::optional<s::SavedMetasearchResultHandle> handle(std::move(*retained));QVERIFY(handle->current(*profile.lease,Session));server.body=metainfo();server.holdBody=mode==2;std::stop_source downloadReceiver;
    auto opened=Binary::openFixture(*profile.lease,profile.path.toStdString(),Session,*handle,profile.endpoint(),downloadReceiver.get_token());QVERIFY(opened);auto owner=std::move(*opened);BinaryPump pump{*owner,profile};
    if(mode==1){service->cancel();QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source&&!pump.completion);QCOMPARE(owner->observation().gets,std::size_t{0});}
    else if(mode==2){QVERIFY(advanceTo(pump,Phase::Reading));QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1,10000);auto next=service->beginSearch(*profile.lease,Session,{f::Profile::selector()},"replacement",receiver.get_token());QVERIFY(next&&*next!=key.queryNonce);QVERIFY(!handle->current(*profile.lease,Session));QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source&&!pump.completion);}
    else{QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);QCOMPARE(pump.completion->bytes,metainfo());QCOMPARE(pump.completion->gets,std::size_t{1});QCOMPARE(pump.completion->session,Session);QVERIFY(server.requests.back().startsWith("GET /owned/result.torrent HTTP/1.1\r\n"));}
    owner->close();QVERIFY(owner->observation().joined);server.release();QCOMPARE(f::bytes(profile.path+"/metasearch.config"),saved);QVERIFY(service->close());handle.reset();profile.lease.reset();auto freshLease=biglybtc::storage::ProfileLease::acquire(profile.path.toStdString());QVERIFY(freshLease);profile.lease.emplace(std::move(*freshLease));owner.reset();service.reset();QCOMPARE(made,1);
  }
  void queryWireLimitsUtf8AndFirstTerminalStayClosed_data(){QTest::addColumn<int>("mode");QTest::newRow("gzip-decoded-cap")<<0;QTest::newRow("redirect-count-cap")<<1;QTest::newRow("malformed-utf8")<<2;QTest::newRow("bomless-utf16-nul")<<3;QTest::newRow("first-http-failure-sticky")<<4;}
  void queryWireLimitsUtf8AndFirstTerminalStayClosed(){
    QFETCH(int,mode);w::Server server;server.body=json();if(mode==0){
      const QByteArray atLimit(4*1024*1024,'x');
      const auto response=[](const QByteArray& encoded){return QByteArray("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: ")+QByteArray::number(encoded.size())+"\r\nConnection: close\r\n\r\n"+encoded;};
      const auto complete=biglybtc::services::http::decodeResponse(response(w::gzip(atLimit)),true,biglybtc::services::http::ResponseProfile::RssDocument);
      QVERIFY(complete.complete&&!complete.unsupported&&!complete.failed);QCOMPARE(complete.bytes,atLimit);
      server.body=w::gzip(atLimit+'x');server.encoding="gzip";
      const auto capped=biglybtc::services::http::decodeResponse(response(server.body),true,biglybtc::services::http::ResponseProfile::RssDocument);
      QVERIFY(!capped.complete&&capped.unsupported&&capped.bytes.isEmpty());
    }if(mode==1){server.redirectEachRequest=true;}if(mode==2){server.body=QByteArray::fromHex("ff");}if(mode==3){server.body=QByteArray::fromHex("3c007200730073003e003c002f007200730073003e00");}if(mode==4){server.rawReply="HTTP/1.1 500 Failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";}
    f::Profile profile(server.url("/query?q=%s"));auto opened=query(profile,profile.endpoint());QVERIFY(opened);auto owner=std::move(*opened);QueryPump pump{*owner,profile};QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(pump.failure&&!pump.completion);QVERIFY(owner->observation().joined);QCOMPARE(*pump.failure,mode==1?Error::Limit:(mode==2||mode==3)?Error::Encoding:Error::Http);if(mode==1){QVERIFY(server.requests.size()<=6);}if(mode==4){owner->requestCancel();QVERIFY(owner->observation().failure==Error::Http);const auto again=owner->poll(*profile.lease,Session);QVERIFY(!again&&again.error()==Error::Http);owner->close();QVERIFY(owner->observation().failure==Error::Http);}closeAndRelease(owner,profile);
  }
};
QTEST_GUILESS_MAIN(MetasearchTransportTests)
#include "metasearch_transport_tests.moc"

