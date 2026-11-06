// SPDX-License-Identifier: GPL-2.0-or-later
#include "rss_http_fetch_fixture.hpp"
#include <QtTest>
#include <stop_token>
#include <thread>
#include <QElapsedTimer>

namespace {
namespace f=rss_fixture;
namespace s=biglybtc::search;
using Fetch=s::RssHttpFetch;using Phase=s::RssFetchPhase;using Error=s::RssFetchError;
auto open(f::Profile& source,Fetch::PrivateEndpointFixture endpoint,std::stop_token stop={}){
  return Fetch::openFixture(*source.lease,source.path.toStdString(),71,f::Profile::selector(),std::move(endpoint),stop);
}
struct Pump {
  Fetch& owner;f::Profile& source;std::optional<s::RssFetchCompletion> completion;std::optional<Error> failure;
  Pump(Fetch& value,f::Profile& profile):owner(value),source(profile){}
  bool step(){auto value=owner.poll(*source.lease,71);if(!value){failure=value.error();return false;}if(*value){completion.emplace(std::move(**value));}return true;}
};
// Approve one earlier checkpoint, then wait without polling until it advances.
// This prevents a phase-peek/poll race from accidentally approving the exact
// BeforeGET/BeforeDelivery checkpoint whose source is about to be revoked.
bool advanceTo(Pump& pump,Phase wanted,std::uint64_t wantedHop=1){
  QElapsedTimer elapsed;elapsed.start();
  while(elapsed.elapsed()<10000){
    const auto before=pump.owner.observation();
    if(before.phase==wanted&&before.hop>=wantedHop){return true;}
    if(before.failure){pump.step();return false;}
    if(before.phase==Phase::BeforeDNS||before.phase==Phase::BeforeConnect||before.phase==Phase::BeforeGET){
      if(!pump.step()||pump.completion){return false;}
      while(elapsed.elapsed()<10000){const auto after=pump.owner.observation();if(after.phase!=before.phase||after.hop!=before.hop||after.failure){break;}QTest::qWait(1);}
    }else{QTest::qWait(1);}
  }return false;
}
void revoke(f::Profile& source){f::write(source.path+"/biglybt.config",f::dict({{"unrelated-setting-byte",f::b("new-source")}}));}
void assertJoinedAndLockReleased(std::unique_ptr<Fetch>& owner,f::Profile& profile){
  owner->close();f::require(owner->observation().joined,"Owned network event loop did not join");profile.lease.reset();
  auto fresh=biglybtc::storage::ProfileLease::acquire(profile.path.toStdString());f::require(bool(fresh),"Closed object retained a real private profile lock alias");owner.reset();profile.lease.emplace(std::move(*fresh));
}
} // namespace

class RssHttpFetchTests final:public QObject {
  Q_OBJECT
private slots:
  void actualSavedGetHasNoTrackerFieldsAndParsesOwningValues_data(){
    QTest::addColumn<int>("mode");QTest::newRow("rss-content-length")<<0;QTest::newRow("rss-chunked")<<1;QTest::newRow("rss-gzip")<<2;QTest::newRow("atom")<<3;
  }
  void actualSavedGetHasNoTrackerFieldsAndParsesOwningValues(){
    QFETCH(int,mode);f::Server server;
    if(mode==1){server.chunked=true;}if(mode==2){server.encoding="gzip";server.body=f::gzip(server.body);}
    if(mode==3){server.body="<feed xmlns=\"http://www.w3.org/2005/Atom\"><entry><title>Atom</title><link rel=\"enclosure\" type=\"application/x-bittorrent\" href=\"https://atom.example.org/owned.torrent\"/></entry></feed>";}
    f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);
    QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.failure.isEmpty());const auto request=server.requests.front();
    QVERIFY(request.startsWith("GET /configured/feed.xml?literal=1 HTTP/1.1\r\n"));
    QVERIFY(request.contains("\r\nHost: feeds.example.org:"+QByteArray::number(server.serverPort())+"\r\n"));QVERIFY(!request.contains("info_hash="));
    const auto& result=*pump.completion;QCOMPARE(result.gets,std::size_t{1});QCOMPARE(result.session,std::uint64_t{71});
    QCOMPARE(result.initialUrl,QUrl(QString::fromStdString(server.url())));QCOMPARE(result.finalUrl,result.initialUrl);
    QCOMPARE(result.context.basePage,QStringLiteral("http://feeds.example.org:")+QString::number(server.serverPort())+"/configured/");
    QCOMPARE(result.document.results.size(),std::size_t{1});QVERIFY(result.document.results[0].title==(mode==3?QStringLiteral("Atom"):QStringLiteral("Owned")));
    QVERIFY(result.document.results[0].torrentLink==(mode==3?QStringLiteral("https://atom.example.org/owned.torrent"):result.context.basePage+"relative.torrent"));
    QVERIFY(result.document.source.contains(mode==3?"<feed":"<rss"));QVERIFY(owner->observation().sourceChecks>=3);
    QVERIFY(!owner->observation().failure);owner->close();QVERIFY(!owner->observation().failure);
    assertJoinedAndLockReleased(owner,profile);
  }
  void redirectsKeepConfiguredRootBaseAndFirstHtmlBaseIsSeparate_data(){QTest::addColumn<bool>("htmlBase");QTest::newRow("configured-prefixes")<<false;QTest::newRow("first-base-only")<<true;}
  void redirectsKeepConfiguredRootBaseAndFirstHtmlBaseIsSeparate(){
    QFETCH(bool,htmlBase);f::Server origin,final;origin.location=QByteArray::fromStdString(final.url("/delivered/feed.xml"));
    if(htmlBase){final.body="<rss><channel><base href=\"https://base.example.org/first/\"/><base href=\"https://wrong.example.org/second/\"/><item><title>Owned</title><enclosure type=\"application/x-bittorrent\" url=\"relative.torrent\"/></item></channel></rss>";}
    f::Profile profile(origin.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);const auto& result=*pump.completion;
    QCOMPARE(result.initialUrl,QUrl(QString::fromStdString(origin.url())));QCOMPARE(result.finalUrl,QUrl(QString::fromStdString(final.url("/delivered/feed.xml"))));
    QCOMPARE(result.context.rootPage,QStringLiteral("http://feeds.example.org:")+QString::number(origin.serverPort()));
    QCOMPARE(result.context.basePage,htmlBase?QStringLiteral("https://base.example.org/first/"):result.context.rootPage+"/configured/");
    QVERIFY(result.document.results.front().torrentLink==result.context.basePage+"relative.torrent");QCOMPARE(result.gets,std::size_t{2});
    QCOMPARE(origin.requests.size(),qsizetype{1});QCOMPARE(final.requests.size(),qsizetype{1});QVERIFY(origin.failure.isEmpty()&&final.failure.isEmpty());assertJoinedAndLockReleased(owner,profile);
  }
  void realTlsTrustAndHostnameAreRequiredBeforeAnyGet_data(){QTest::addColumn<int>("mode");QTest::newRow("saved-root-and-hostname")<<0;QTest::newRow("unknown-root-no-GET")<<1;QTest::newRow("wrong-hostname-no-GET")<<2;}
  void realTlsTrustAndHostnameAreRequiredBeforeAnyGet(){
    QFETCH(int,mode);QVERIFY(QSslSocket::supportsSsl());f::Certificate identity(mode==2?"wrong.example.org":"feeds.example.org"),unrelated;
    f::Server server(&identity);f::Profile profile(server.url(),mode==1?&unrelated.certificate:&identity.certificate);
    auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);
    if(mode==0){QVERIFY(!pump.failure&&pump.completion);QCOMPARE(server.requests.size(),qsizetype{1});}
    else{QVERIFY(pump.failure&&*pump.failure==Error::Tls);QVERIFY(!pump.completion);QCOMPARE(server.requests.size(),qsizetype{0});QVERIFY(server.connections>0);}
    assertJoinedAndLockReleased(owner,profile);
  }
  void actualSourceRevocationAtHeldPhasesDropsCompletionAndRecovers_data(){
    QTest::addColumn<int>("mode");QTest::newRow("before-DNS")<<0;QTest::newRow("after-TCP-before-GET")<<1;QTest::newRow("after-TLS-before-GET")<<2;
    QTest::newRow("partial-real-body")<<3;QTest::newRow("before-delivery")<<4;QTest::newRow("held-resolver")<<5;
  }
  void actualSourceRevocationAtHeldPhasesDropsCompletionAndRecovers(){
    QFETCH(int,mode);f::Certificate identity;f::Server server(mode==2?&identity:nullptr);server.holdBody=mode==3;
    f::Profile profile(server.url(),mode==2?&identity.certificate:nullptr);f::HeldResolver resolver;
    auto made=open(profile,mode==5?resolver.endpoint():profile.endpoint());QVERIFY(made);auto owner=std::move(*made);const auto firstNonce=owner->observation().nonce;Pump pump{*owner,profile};
    if(mode==1||mode==2||mode==4){const auto wanted=mode==4?Phase::BeforeDelivery:Phase::BeforeGET;
      QVERIFY(advanceTo(pump,wanted));QVERIFY(!pump.failure);QVERIFY(owner->observation().phase==wanted);
      if(mode==2){QTRY_VERIFY_WITH_TIMEOUT(server.encrypted>0,1000);QCOMPARE(server.requests.size(),qsizetype{0});}
    }else if(mode==3){QVERIFY(advanceTo(pump,Phase::Reading));QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1&&server.requests.size()==1,10000);QVERIFY(!pump.failure);}
    else if(mode==5){QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);QVERIFY(!pump.failure);QCOMPARE(server.connections,0);}
    revoke(profile);QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source);QVERIFY(!pump.completion);QVERIFY(owner->observation().joined);
    if(mode<=2||mode==5){QCOMPARE(server.requests.size(),qsizetype{0});}else{QCOMPARE(server.requests.size(),qsizetype{1});}
    server.release();resolver.release();owner->close();owner.reset();server.holdBody=false;
    auto recovered=open(profile,profile.endpoint());QVERIFY(recovered);owner=std::move(*recovered);Pump fresh{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);QVERIFY(!fresh.failure&&fresh.completion);QVERIFY(fresh.completion->nonce!=firstNonce);
    assertJoinedAndLockReleased(owner,profile);QVERIFY(server.failure.isEmpty());
  }
  void unrelatedProfileEpochBeforeGetDoesNotPoisonExactSource(){
    f::Server server;f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QVERIFY(advanceTo(pump,Phase::BeforeGET));QVERIFY(!pump.failure);QCOMPARE(server.requests.size(),qsizetype{0});
    auto other=biglybtc::storage::ConfigFile::createMissing(*profile.lease,"unrelated.config","d6:futurei1ee");QVERIFY(other);
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(!pump.failure&&pump.completion);QCOMPARE(server.requests.size(),qsizetype{1});assertJoinedAndLockReleased(owner,profile);
  }
  void receiverCancellationDiscardsLateResolverAndBusySlotThenReleases(){
    f::Server server;f::Profile profile(server.url());f::HeldResolver resolver;std::stop_source receiver;
    auto made=open(profile,resolver.endpoint(),receiver.get_token());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    auto concurrent=open(profile,profile.endpoint());QVERIFY(!concurrent&&concurrent.error()==Error::Busy);
    QVERIFY(advanceTo(pump,Phase::Resolving));QTRY_VERIFY_WITH_TIMEOUT(resolver.count()==1,1000);QVERIFY(!pump.failure);receiver.request_stop();
    QVERIFY(resolver.release()>0);QTRY_VERIFY_WITH_TIMEOUT(resolver.callbacksForwarded.load()==1,10000);
    QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled);QCOMPARE(server.connections,0);QCOMPARE(server.requests.size(),qsizetype{0});QVERIFY(owner->observation().joined);
    owner->close();owner.reset();auto next=open(profile,profile.endpoint());QVERIFY(next);owner=std::move(*next);Pump fresh{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!fresh.step()||fresh.completion,10000);QVERIFY(!fresh.failure&&fresh.completion);assertJoinedAndLockReleased(owner,profile);
  }
  void externalReceiverCancelAbortsHeldBodyAndJoinsBeforeLockRelease(){
    f::Server server;server.holdBody=true;f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QVERIFY(advanceTo(pump,Phase::Reading));QTRY_VERIFY_WITH_TIMEOUT(server.bodyPrefixBytes==1&&server.requests.size()==1,10000);QVERIFY(!pump.failure);
    std::thread cancelled([&]{owner->requestCancel();});cancelled.join();QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Cancelled);QVERIFY(!pump.completion);
    server.release();QCOMPARE(server.requests.size(),qsizetype{1});assertJoinedAndLockReleased(owner,profile);
  }
  void realResponseAndRedirectBoundsCannotBecomeEmptySuccess_data(){
    QTest::addColumn<int>("mode");QTest::newRow("conflicting-length")<<0;QTest::newRow("truncated-body")<<1;QTest::newRow("unsupported-encoding")<<2;
    QTest::newRow("decoded-gzip-over-four-MiB")<<3;QTest::newRow("header-over-sixteen-KiB")<<4;QTest::newRow("redirect-loop")<<5;QTest::newRow("maximum-six-GETs")<<6;
  }
  void realResponseAndRedirectBoundsCannotBecomeEmptySuccess(){
    QFETCH(int,mode);f::Server server;
    if(mode==0){server.rawReply="HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 1\r\nConnection: close\r\n\r\nx";}
    else if(mode==1){server.rawReply="HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n<rss/>";}
    else if(mode==2){server.encoding="br";}
    else if(mode==3){server.encoding="gzip";server.body=f::gzip(QByteArray(4*1024*1024+1,'x'));}
    else if(mode==4){server.rawReply="HTTP/1.1 200 OK\r\nX-Owned: "+QByteArray(16384,'x')+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";}
    else if(mode==5){server.location=QByteArray::fromStdString(server.url());}
    else{server.redirectEachRequest=true;}
    f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(pump.failure&&!pump.completion);
    if(mode<=2){QVERIFY(*pump.failure==Error::Http);}else if(mode==3){QVERIFY(*pump.failure==Error::Http||*pump.failure==Error::Limit);}else{QVERIFY(*pump.failure==Error::Limit);}
    QCOMPARE(server.requests.size(),mode==6?qsizetype{6}:qsizetype{1});QVERIFY(server.failure.isEmpty());assertJoinedAndLockReleased(owner,profile);
  }
  void redirectedSourceMustStillBeCurrentBeforeNextDns(){
    f::Server origin,destination;origin.location=QByteArray::fromStdString(destination.url());f::Profile profile(origin.url());
    auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QVERIFY(advanceTo(pump,Phase::BeforeDNS,2));QVERIFY(!pump.failure);
    QCOMPARE(origin.requests.size(),qsizetype{1});QCOMPARE(destination.connections,0);revoke(profile);QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Source);QCOMPARE(destination.connections,0);assertJoinedAndLockReleased(owner,profile);
  }
  void unapprovedBeforeDnsUsesTheRealConfiguredThirtySecondPhaseDeadline(){
    f::Server server;f::Profile profile(server.url());QElapsedTimer elapsed;elapsed.start();
    auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);
    QTRY_VERIFY_WITH_TIMEOUT(owner->observation().failure.has_value(),40000);
    const auto observed=elapsed.elapsed();const auto view=owner->observation();
    qInfo()<<"unapproved-BeforeDNS configured_phase_ms=30000 observed_ms="<<observed;
    // QTimer's default CoarseTimer may fire up to5% early. Record the first
    // observed terminal directly; never delay observation to manufacture30s.
    QVERIFY(observed>=28000&&observed<40000);QVERIFY(view.failure&&*view.failure==Error::Deadline);
    QCOMPARE(view.gets,std::size_t{0});QCOMPARE(view.resolverSubmissions,std::size_t{0});QCOMPARE(server.connections,0);
    Pump pump{*owner,profile};QVERIFY(!pump.step());QVERIFY(pump.failure&&*pump.failure==Error::Deadline);
    owner->requestCancel();const auto afterCancel=owner->poll(*profile.lease,71);QVERIFY(!afterCancel&&afterCancel.error()==Error::Deadline);
    QVERIFY(owner->observation().failure==Error::Deadline);owner->close();QVERIFY(owner->observation().failure==Error::Deadline);
    assertJoinedAndLockReleased(owner,profile);
  }
  void firstRealHttpFailureRemainsStickyAcrossCancelAndClose(){
    f::Server server;server.rawReply="HTTP/1.1 500 Owned failure\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(pump.failure&&*pump.failure==Error::Http);QVERIFY(!pump.completion);
    const auto before=owner->observation();QVERIFY(before.failure==Error::Http);QCOMPARE(server.requests.size(),qsizetype{1});
    owner->requestCancel();const auto afterCancel=owner->poll(*profile.lease,71);QVERIFY(!afterCancel&&afterCancel.error()==Error::Http);
    QVERIFY(owner->observation().failure==Error::Http);owner->close();QVERIFY(owner->observation().failure==Error::Http);
    QCOMPARE(owner->observation().gets,before.gets);assertJoinedAndLockReleased(owner,profile);
  }
  void bomlessUtf16CannotBecomeADeclaredUtf8Feed(){
    f::Server server;const auto ascii=f::rss();QByteArray utf16;
    for(const char c:ascii){utf16+=c;utf16+='\0';}server.body=utf16;
    f::Profile profile(server.url());auto made=open(profile,profile.endpoint());QVERIFY(made);auto owner=std::move(*made);Pump pump{*owner,profile};
    QTRY_VERIFY_WITH_TIMEOUT(!pump.step()||pump.completion,10000);QVERIFY(pump.failure&&(*pump.failure==Error::Encoding||*pump.failure==Error::Parse));QVERIFY(!pump.completion);
    QCOMPARE(server.requests.size(),qsizetype{1});assertJoinedAndLockReleased(owner,profile);
  }
  void unsupportedSavedRoutesAndProxyRequirementsRefuseBeforeResolver_data(){
    QTest::addColumn<int>("mode");QTest::newRow("tor")<<0;QTest::newRow("i2p")<<1;QTest::newRow("onion")<<2;QTest::newRow("numeric-loopback")<<3;
    QTest::newRow("private-v4")<<4;QTest::newRow("saved-proxy-type")<<5;QTest::newRow("legacy-proxy-required")<<6;
  }
  void unsupportedSavedRoutesAndProxyRequirementsRefuseBeforeResolver(){
    QFETCH(int,mode);f::Server server;auto url=server.url();
    if(mode==0){url="tor:"+url;}else if(mode==1){url="i2p:"+url;}else if(mode==2){url="http://owned.onion/f";}
    else if(mode==3){url="http://127.0.0.1:"+std::to_string(server.serverPort())+"/f";}else if(mode==4){url="http://10.0.0.1/f";}
    f::Profile profile(url);if(mode==5||mode==6){f::write(profile.path+"/biglybt.config",f::dict({{mode==5?"metasearch.config.proxy.type":"metasearch.config.proxy.enable","i1e"}}));}
    f::HeldResolver resolver;auto made=open(profile,resolver.endpoint());QVERIFY(!made);QVERIFY(made.error()==Error::Source||made.error()==Error::UnsupportedRoute);QCOMPARE(resolver.count(),std::size_t{0});QCOMPARE(server.connections,0);QCOMPARE(server.requests.size(),qsizetype{0});
  }
};
QTEST_GUILESS_MAIN(RssHttpFetchTests)
#include "rss_http_fetch_tests.moc"
