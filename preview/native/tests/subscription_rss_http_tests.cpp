// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_rss_http_fixture.hpp"
#include <QtTest>
#include <QElapsedTimer>
#include <stop_token>

namespace {
namespace f=subscription_http_fixture;
namespace wire=rss_fixture;
namespace s=biglybtc::search;
using Fetch=s::RssHttpFetch;using Phase=s::RssFetchPhase;using Error=s::RssFetchError;
constexpr std::uint64_t session=113;
auto open(f::Profile& source,Fetch::PrivateEndpointFixture endpoint,std::stop_token stop={}){
  return Fetch::openSubscriptionFixture(*source.lease,source.path.toStdString(),session,source.selector(),std::move(endpoint),stop);
}
struct Pump {
  Fetch& owner;f::Profile& source;std::optional<s::RssFetchCompletion> completion;std::optional<Error> failure;
  Pump(Fetch& fetch,f::Profile& profile):owner(fetch),source(profile){}
  bool step(){auto value=owner.poll(*source.lease,session);if(!value){failure=value.error();return false;}if(*value){completion.emplace(std::move(**value));}return true;}
};
bool advanceTo(Pump& pump,Phase wanted){
  QElapsedTimer elapsed;elapsed.start();
  while(elapsed.elapsed()<10000){
    const auto before=pump.owner.observation();if(before.phase==wanted){return true;}
    if(before.failure){pump.step();return false;}
    if(before.phase==Phase::BeforeDNS||before.phase==Phase::BeforeConnect||before.phase==Phase::BeforeGET){
      if(!pump.step()||pump.completion){return false;}
      while(elapsed.elapsed()<10000){const auto after=pump.owner.observation();if(after.phase!=before.phase||after.hop!=before.hop||after.failure){break;}QTest::qWait(1);}
    }else{QTest::qWait(1);}
  }return false;
}
void closeAndReacquire(std::unique_ptr<Fetch>& owner,f::Profile& profile){
  owner->close();wire::require(owner->observation().joined,"Subscription socket/context did not join");profile.lease.reset();
  auto held=biglybtc::storage::ProfileLease::acquire(profile.path.toStdString());
  wire::require(bool(held),"Closed subscription variant retained an actual OFD alias");owner.reset();profile.lease.emplace(std::move(*held));
}
} // namespace

class SubscriptionRssHttpTests final:public QObject {
  Q_OBJECT
private slots:
  void actualEmbeddedAndCatalogSourcesChooseOnlyTheirOwnLiteralGet_data(){
    QTest::addColumn<bool>("catalog");QTest::newRow("embedded-with-no-metasearch-file")<<false;QTest::newRow("actual-catalog-precedes-embedded")<<true;
  }
  void actualEmbeddedAndCatalogSourcesChooseOnlyTheirOwnLiteralGet(){
    QFETCH(bool,catalog);wire::Server embedded,selected;f::Profile source(embedded.url());
    if(catalog){source.catalog(selected.url("/catalog/feed.xml?literal=1"));}else{QVERIFY(!QFile::exists(source.path+"/metasearch.config"));}
    {
      auto real=s::SavedSubscriptionSourceSnapshot::read(*source.lease,source.path.toStdString(),session,source.selector());QVERIFY(real);
      QCOMPARE(real->description().embeddedEngine,!catalog);QVERIFY(real->description().selected==source.selector());
    }
    const auto before=source.sourceBytes();auto made=open(source,source.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,source};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);
    QCOMPARE(embedded.requests.size(),qsizetype{catalog?0:1});QCOMPARE(selected.requests.size(),qsizetype{catalog?1:0});
    const auto& request=catalog?selected.requests.front():embedded.requests.front();
    QVERIFY(request.startsWith(catalog?"GET /catalog/feed.xml?literal=1 HTTP/1.1\r\n":"GET /configured/feed.xml?literal=1 HTTP/1.1\r\n"));
    QVERIFY(!request.contains("info_hash=")&&!request.contains("HEAD "));
    QCOMPARE(pump.completion->session,session);QCOMPARE(pump.completion->gets,std::size_t{1});
    QCOMPARE(pump.completion->initialUrl,QUrl(QString::fromStdString(catalog?selected.url("/catalog/feed.xml?literal=1"):embedded.url())));
    QCOMPARE(pump.completion->document.results.size(),std::size_t{1});QVERIFY(pump.completion->document.results[0].title==QStringLiteral("Owned"));
    QVERIFY(source.sourceBytes()==before);QVERIFY(owner->observation().sourceChecks>=3);closeAndReacquire(owner,source);
    QVERIFY(embedded.failure.isEmpty()&&selected.failure.isEmpty());
  }
  void actualSubscriptionSourcesRevokeAtOwnedTransportPhases_data(){
    QTest::addColumn<int>("mode");QTest::newRow("before-DNS")<<0;QTest::newRow("after-TCP-before-GET")<<1;QTest::newRow("after-TLS-before-GET")<<2;
    QTest::newRow("held-partial-body")<<3;QTest::newRow("before-delivery")<<4;QTest::newRow("held-context-resolver")<<5;
  }
  void actualSubscriptionSourcesRevokeAtOwnedTransportPhases(){
    QFETCH(int,mode);wire::Certificate identity;wire::Server server(mode==2?&identity:nullptr);server.holdBody=mode==3;
    f::Profile source(server.url(),mode==2?&identity.certificate:nullptr);wire::HeldResolver resolver;
    auto made=open(source,mode==5?resolver.endpoint():source.endpoint());QVERIFY(made);auto owner=std::move(*made);const auto oldNonce=owner->observation().nonce;Pump pump{*owner,source};
    if(mode==1||mode==2||mode==4){QVERIFY(advanceTo(pump,mode==4?Phase::BeforeDelivery:Phase::BeforeGET));QVERIFY(!pump.failure);
      if(mode==2){QTRY_VERIFY_WITH_TIMEOUT(server.encrypted>0,1000);QCOMPARE(server.requests.size(),qsizetype{0});}}
    else if(mode==3){QVERIFY(advanceTo(pump,Phase::Reading));QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1&&server.requests.size()==1,10000);}
    else if(mode==5){QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);QCOMPARE(server.connections,0);}
    if(mode==0||mode==5){wire::write(source.path+"/subscriptions.config",source.configuration+"x");}
    else if(mode==2){wire::write(source.path+"/biglybt.config",f::dictionary({{"actual-source-change",wire::b("changed")}}));}
    else{wire::write(source.bodyPath,source.body+"x");}
    QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source);QVERIFY(!pump.completion);QVERIFY(owner->observation().joined);
    QCOMPARE(server.requests.size(),qsizetype{(mode==3||mode==4)?1:0});
    owner->close();owner.reset();server.release();resolver.release();server.holdBody=false;source.restore();
    auto recovered=open(source,source.endpoint());QVERIFY(recovered);owner=std::move(*recovered);Pump fresh{*owner,source};
    QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=oldNonce);
    closeAndReacquire(owner,source);QVERIFY(server.failure.isEmpty());
  }
  void unsupportedAcquisitionOrWrongSavedIdentityRefusesBeforeResolver_data(){
    QTest::addColumn<int>("mode");QTest::newRow("automatic-download-policy")<<0;QTest::newRow("wrong-raw-short-SID")<<1;
    QTest::newRow("wrong-raw-public-key")<<2;QTest::newRow("wrong-version")<<3;QTest::newRow("wrong-source-index")<<4;
    QTest::newRow("embedded-I2P-route")<<5;QTest::newRow("saved-proxy-required")<<6;
  }
  void unsupportedAcquisitionOrWrongSavedIdentityRefusesBeforeResolver(){
    QFETCH(int,mode);wire::Server server;f::Profile source(mode==5?std::string("http://owned.i2p/feed.xml"):server.url(),nullptr,mode==0);auto selected=source.selector();
    if(mode==6){wire::write(source.path+"/biglybt.config",f::dictionary({{"metasearch.config.proxy.type",f::integer(1)}}));}
    if(mode==1){selected.shortId[0]=static_cast<char>(selected.shortId[0]^1);}if(mode==2){selected.publicKey[0]=static_cast<char>(selected.publicKey[0]^1);}
    if(mode==3){++selected.version;}if(mode==4){++selected.sourceIndex;}
    const auto before=source.sourceBytes();std::atomic<int> submissions{0};auto endpoint=source.endpoint();auto resolver=std::move(endpoint.resolve);
    endpoint.resolve=[&](const QString& host,QObject* context,Fetch::PrivateEndpointFixture::Completion completion){++submissions;return resolver(host,context,std::move(completion));};
    auto made=Fetch::openSubscriptionFixture(*source.lease,source.path.toStdString(),session,std::move(selected),std::move(endpoint));
    QVERIFY(!made);QVERIFY(made.error()==Error::Source||(mode>=5&&made.error()==Error::UnsupportedRoute));QCOMPARE(submissions.load(),0);QCOMPARE(server.connections,0);QCOMPARE(server.requests.size(),qsizetype{0});QVERIFY(source.sourceBytes()==before);
    source.lease.reset();auto held=biglybtc::storage::ProfileLease::acquire(source.path.toStdString());QVERIFY(held);
  }
  void cancelledSubscriptionResolverCannotDeliverAndFreshNonceRecovers(){
    wire::Server server;f::Profile source(server.url());wire::HeldResolver resolver;std::stop_source receiver;
    auto made=open(source,resolver.endpoint(),receiver.get_token());QVERIFY(made);auto owner=std::move(*made);const auto oldNonce=owner->observation().nonce;Pump pump{*owner,source};
    QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);receiver.request_stop();
    QVERIFY(resolver.release()>0);QTRY_VERIFY_WITH_TIMEOUT(resolver.callbacksForwarded.load()==1,10000);
    QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled);QVERIFY(!pump.completion);QVERIFY(owner->observation().joined);QCOMPARE(server.connections,0);
    owner->close();owner.reset();auto recovered=open(source,source.endpoint());QVERIFY(recovered);owner=std::move(*recovered);Pump fresh{*owner,source};
    QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=oldNonce);closeAndReacquire(owner,source);
  }
  void unrelatedProfileWriteRefreshesSubscriptionObservationAndCloseReleasesAliases(){
    wire::Server server;f::Profile source(server.url());const auto before=source.sourceBytes();auto made=open(source,source.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,source};
    QVERIFY(advanceTo(pump,Phase::BeforeGET));auto other=biglybtc::storage::ConfigFile::createMissing(*source.lease,"unrelated.config","d6:futurei1ee");QVERIFY(other);
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(source.sourceBytes()==before);
    closeAndReacquire(owner,source);
  }
};
QTEST_GUILESS_MAIN(SubscriptionRssHttpTests)
#include "subscription_rss_http_tests.moc"
