#include "owned_original_mainline.hpp"
#include <QDir>
#include <QFileInfo>
#include "persistent_session.hpp"
#include <libtorrent/settings_pack.hpp>

namespace {
using namespace biglybtc;
namespace lt=libtorrent;
using namespace biglybtc::test::original_mainline;
using namespace std::chrono_literals;
}

class OriginalMainlineWireTests final : public QObject {
  Q_OBJECT
private slots:
#if __has_include(<libtorrent/biglybtc_mainline_identity.hpp>)
  void exactOriginalWireIdentity_data(){
    QTest::addColumn<bool>("v6");QTest::addColumn<bool>("zero");
    QTest::newRow("v4-arbitrary")<<false<<false;QTest::newRow("v4-zero")<<false<<true;
    QTest::newRow("v6-arbitrary")<<true<<false;QTest::newRow("v6-zero")<<true<<true;
  }
  void exactOriginalWireIdentity(){
    QFETCH(bool,v6);QFETCH(bool,zero);const QByteArray key(20,zero?'\0':char(0x5a));Source source(key);Krpc wire(v6,key);
    auto opts=options(source,v6);wire.sourcePort=static_cast<unsigned short>(opts.port);
    opts.bootstrapNodes={{v6?"::1":"127.0.0.1",wire.socket.localPort()}};
    auto made=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY2(made,made?"":made.error().message.c_str());
    auto service=std::move(*made);const QByteArray rawHash(20,'h');const lt::sha1_hash hash(rawHash.constData());
    QVERIFY(service->lookup(hash));bool delivered=false;QString failure;
    auto pump=[&]{auto result=service->poll();if(!result){failure=QString::fromStdString(result.error().message);return false;}
      for(const auto& row:*result){if(row.hash==hash&&!row.peers.empty()){delivered=true;}}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(pump()&&delivered,15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY2(wire.failure.isEmpty(),qPrintable(wire.failure));
    QVERIFY(wire.queries>0);QVERIFY(wire.gets>0);QVERIFY(wire.bootstraps>0);const auto active=service->status();
    QVERIFY(active.originalIdentityRequested);QVERIFY(active.originalIdentityActive);QCOMPARE(active.originalIdentityAdoptions,std::uint64_t{1});
    QCOMPARE(active.listenPort,static_cast<unsigned short>(opts.port));QCOMPARE(active.lookups,std::uint64_t{1});
    QCOMPARE(active.announces,std::uint64_t{0});QVERIFY(source.unchanged());
    auto checkpoint=service->checkpoint();QVERIFY(checkpoint);auto document=core::Document::parse(*checkpoint);QVERIFY(document);
    const auto* dht=document->find("dht state");QVERIFY(dht);const auto* ids=dht->find("node-id");QVERIFY(!ids||ids->children.empty());
    auto duplicate=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY(!duplicate);QCOMPARE(duplicate.error().code,storage::ErrorCode::Busy);
    QVERIFY(service->close());const auto closed=service->status();QVERIFY(closed.originalIdentityJoined);QVERIFY(!closed.originalIdentityActive);
    QCOMPARE(closed.originalIdentityRetirements,std::uint64_t{1});QVERIFY(source.unchanged());
    // Actual join releases the original family slot. The same retained source
    // can be freshly installed; this is not a reader-only reservation claim.
    auto next=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY(next);QVERIFY((*next)->close());QVERIFY(source.unchanged());
  }
  void changedHeldSourceRetiresActualOriginalNodeWithoutFallback(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);
    auto made=engine::MainlineDhtService::create(policy(false),opts);QVERIFY2(made,made?"":made.error().message.c_str());
    QString failure;auto listening=[&]{auto observed=(*made)->poll();
      if(!observed){failure=QString::fromStdString(observed.error().message);return false;}
      return (*made)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(listening(),5000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    QFile original(source.leaf);QVERIFY(original.rename(source.leaf+".old"));QFile replacement(source.leaf);
    QVERIFY(replacement.open(QIODevice::WriteOnly));QCOMPARE(replacement.write(source.bytes),source.bytes.size());replacement.close();
    const auto denied=(*made)->poll();QVERIFY(!denied);const auto stopped=(*made)->status();
    QCOMPARE(stopped.phase,engine::MainlineDhtPhase::Failed);QVERIFY(stopped.originalIdentityJoined);
    QVERIFY(!stopped.originalIdentityActive);QCOMPARE(stopped.originalIdentityAdoptions,std::uint64_t{1});
    QCOMPARE(stopped.originalIdentityRetirements,std::uint64_t{1});
  }
  void currentSourceAndOneSocketPerFamilyAreRequiredBeforeConstruction(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);auto multiple=policy(false);multiple.bindAddresses.push_back("127.0.0.2");
    auto ambiguous=engine::MainlineDhtService::create(multiple,opts);
    QVERIFY(!ambiguous);QCOMPARE(ambiguous.error().code,storage::ErrorCode::Unsupported);
    QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".original"));QFile replacement(source.leaf);
    QVERIFY(replacement.open(QIODevice::WriteOnly));QCOMPARE(replacement.write(source.bytes),source.bytes.size());replacement.close();
    auto stale=engine::MainlineDhtService::create(policy(false),opts);QVERIFY(!stale);QCOMPARE(stale.error().code,storage::ErrorCode::Conflict);
  }

#if __has_include(<libtorrent/biglybtc_mainline_routing.hpp>)
  void twoFamiliesShareOriginalSlotZeroWithoutSharingRoutingAuthority_data(){
    QTest::addColumn<bool>("zero");QTest::newRow("same-arbitrary-id")<<false;QTest::newRow("same-zero-id")<<true;
  }
  void twoFamiliesShareOriginalSlotZeroWithoutSharingRoutingAuthority(){
    QFETCH(bool,zero);const QByteArray key(20,zero?'\0':char(0x5a));Source source(key);Krpc v4(false,key),v6(true,key);
    auto opts=options(source,false);opts.ipv6=true;
    // Verify both explicit binds are free at the SAME chosen service port.
    {QUdpSocket proof;QVERIFY(proof.bind(QHostAddress::LocalHostIPv6,static_cast<quint16>(opts.port),QUdpSocket::DontShareAddress));}
    auto selected=policy(false);selected.ipv6=true;selected.mainlineIpv6=true;selected.bindAddresses.push_back("::1");
    v4.sourcePort=v6.sourcePort=static_cast<unsigned short>(opts.port);
    auto made=engine::MainlineDhtService::create(selected,opts);QVERIFY2(made,made?"":made.error().message.c_str());
    auto service=std::move(*made);const std::string rawHash(20,'h');const lt::sha1_hash hash(rawHash.data());
    QVERIFY(service->addPeerNode({"127.0.0.1",v4.socket.localPort()}));QVERIFY(service->addPeerNode({"::1",v6.socket.localPort()}));
    QString failure;bool delivered4=false,delivered6=false;
    auto pump=[&]{auto result=service->poll();if(!result){failure=QString::fromStdString(result.error().message);return false;}
      for(const auto& row:*result){for(const auto& peer:row.peers){if(row.hash==hash){(peer.address().is_v4()?delivered4:delivered6)=true;}}}return true;};
    // No router or native ID seed. Each real PING response must be proven in
    // its OWN actual active table before this monotonically reaches two.
    QTRY_VERIFY_WITH_TIMEOUT(pump()&&service->status().peerBootstrapProgress==2,15000);
    QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY2(v4.failure.isEmpty(),qPrintable(v4.failure));QVERIFY2(v6.failure.isEmpty(),qPrintable(v6.failure));
    QVERIFY(service->lookup(hash));QTRY_VERIFY_WITH_TIMEOUT(pump()&&delivered4&&delivered6,15000);
    QVERIFY(v4.gets>0);QVERIFY(v6.gets>0);
    auto round=service->scrape(hash,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(round);
    std::vector<engine::MainlineDhtScrape> summaries;
    auto scrape=[&]{if(!pump()){return false;}auto ready=service->takeScrapes();if(!ready){failure=QString::fromStdString(ready.error().message);return false;}
      for(auto& row:*ready){summaries.push_back(std::move(row));}return !summaries.empty();};
    QTRY_VERIFY_WITH_TIMEOUT(scrape(),15000);QCOMPARE(summaries.size(),std::size_t{1});QCOMPARE(summaries[0].round,*round);
    QCOMPARE(summaries[0].summary.responses,std::size_t{2});QCOMPARE(summaries[0].summary.seeds,1);
    QCOMPARE(summaries[0].summary.peers,1);QCOMPARE(summaries[0].summary.direct,std::size_t{0});QVERIFY(!summaries[0].timedOut);
    QCOMPARE(v4.scrapes,1);QCOMPARE(v6.scrapes,1);QVERIFY2(v4.failure.isEmpty(),qPrintable(v4.failure));QVERIFY2(v6.failure.isEmpty(),qPrintable(v6.failure));
    const auto active=service->status();QVERIFY(active.originalIdentityActive);QCOMPARE(active.originalIdentityAdoptions,std::uint64_t{2});
    QCOMPARE(active.announces,std::uint64_t{0});QCOMPARE(active.peerBootstrapProgress,std::uint64_t{2});QVERIFY(source.unchanged());
    // One family already held blocks the dual request atomically; neither
    // family can be silently assigned a new original slot or stock local ID.
    auto duplicate=engine::MainlineDhtService::create(selected,opts);QVERIFY(!duplicate);QCOMPARE(duplicate.error().code,storage::ErrorCode::Busy);
    QVERIFY(service->close());const auto closed=service->status();QVERIFY(closed.originalIdentityJoined);QVERIFY(!closed.originalIdentityActive);
    QCOMPARE(closed.originalIdentityRetirements,std::uint64_t{2});QVERIFY(source.unchanged());
    auto rebound=engine::MainlineDhtService::create(selected,opts);QVERIFY(rebound);
    auto both=[&]{auto result=(*rebound)->poll();return result&&(*rebound)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(both(),5000);QCOMPARE((*rebound)->status().originalIdentityAdoptions,std::uint64_t{2});
    QVERIFY((*rebound)->close());QCOMPARE((*rebound)->status().originalIdentityRetirements,std::uint64_t{2});QVERIFY(source.unchanged());
  }
  void failedDualReservationCannotConsumeTheOtherFamilySlot(){
    Source source(QByteArray(20,'r'));auto v4options=options(source,false);
    auto v4=engine::MainlineDhtService::create(policy(false),v4options);QVERIFY(v4);
    auto one=[&]{auto result=(*v4)->poll();return result&&(*v4)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(one(),5000);
    auto dualOptions=options(source,false);dualOptions.ipv6=true;auto dual=policy(false);
    dual.ipv6=true;dual.mainlineIpv6=true;dual.bindAddresses.push_back("::1");
    auto refused=engine::MainlineDhtService::create(dual,dualOptions);QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Busy);
    auto v6=engine::MainlineDhtService::create(policy(true),options(source,true));QVERIFY(v6);
    auto other=[&]{auto result=(*v6)->poll();return result&&(*v6)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(other(),5000);QVERIFY((*v4)->close());QVERIFY((*v6)->close());
    QCOMPARE((*v4)->status().originalIdentityRetirements,std::uint64_t{1});
    QCOMPARE((*v6)->status().originalIdentityRetirements,std::uint64_t{1});QVERIFY(source.unchanged());
  }
  void twoFamilySourceFailureJoinsAndRetiresBothActualNodes(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);opts.ipv6=true;
    auto selected=policy(false);selected.ipv6=true;selected.mainlineIpv6=true;selected.bindAddresses.push_back("::1");
    auto made=engine::MainlineDhtService::create(selected,opts);QVERIFY(made);
    auto both=[&]{auto result=(*made)->poll();return result&&(*made)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(both(),5000);QCOMPARE((*made)->status().originalIdentityAdoptions,std::uint64_t{2});
    QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".retained"));QFile replacement(source.leaf);
    QVERIFY(replacement.open(QIODevice::WriteOnly));QCOMPARE(replacement.write(source.bytes),source.bytes.size());replacement.close();
    QVERIFY(!(*made)->poll());const auto stopped=(*made)->status();QCOMPARE(stopped.phase,engine::MainlineDhtPhase::Failed);
    QVERIFY(stopped.originalIdentityJoined);QVERIFY(!stopped.originalIdentityActive);QCOMPARE(stopped.originalIdentityRetirements,std::uint64_t{2});
  }
  void oneFamilyListenFailureCannotLeaveTheOtherOriginalIdentityActive_data(){
    QTest::addColumn<bool>("blockedV6");QTest::newRow("v4-listen-failure")<<false;QTest::newRow("v6-listen-failure")<<true;
  }
  void oneFamilyListenFailureCannotLeaveTheOtherOriginalIdentityActive(){
    QFETCH(bool,blockedV6);Source source(QByteArray(20,'r'));auto opts=options(source,false);opts.ipv6=true;
    QUdpSocket blocked;QVERIFY(blocked.bind(blockedV6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,static_cast<quint16>(opts.port),QUdpSocket::DontShareAddress));
    auto selected=policy(false);selected.ipv6=true;selected.mainlineIpv6=true;selected.bindAddresses.push_back("::1");
    auto made=engine::MainlineDhtService::create(selected,opts);QVERIFY(made);bool refused=false;
    auto stopped=[&]{auto result=(*made)->poll();refused=!result;return refused;};QTRY_VERIFY_WITH_TIMEOUT(stopped(),5000);
    QVERIFY(refused);QCOMPARE((*made)->status().phase,engine::MainlineDhtPhase::Failed);QVERIFY((*made)->status().originalIdentityJoined);
    QVERIFY(!(*made)->status().originalIdentityActive);QCOMPARE((*made)->status().originalIdentityAdoptions,(*made)->status().originalIdentityRetirements);
    blocked.close();auto next=engine::MainlineDhtService::create(selected,opts);QVERIFY(next);QVERIFY((*next)->close());QVERIFY(source.unchanged());
  }
#else
  void olderIdentitySdkRefusesOnlyTheUnsupportedDualRequest(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);opts.ipv6=true;
    auto dual=policy(false);dual.ipv6=true;dual.mainlineIpv6=true;dual.bindAddresses.push_back("::1");
    auto denied=engine::MainlineDhtService::create(dual,opts);QVERIFY(!denied);QCOMPARE(denied.error().code,storage::ErrorCode::Unsupported);QVERIFY(source.unchanged());
  }
#endif

  void persistentOwnerRetainsTheSameOfdAndImportsConfiguredOriginalRoot_data(){
    QTest::addColumn<bool>("dual");QTest::addColumn<bool>("custom");
    QTest::newRow("ipv4-same-ofd")<<false<<false;QTest::newRow("configured-plugin-directory")<<false<<true;
#if __has_include(<libtorrent/biglybtc_mainline_routing.hpp>)
    QTest::newRow("dual-same-slot-zero")<<true<<false;
#endif
  }
  void persistentOwnerRetainsTheSameOfdAndImportsConfiguredOriginalRoot(){
    QFETCH(bool,dual);QFETCH(bool,custom);Source source(QByteArray(20,'r'));Krpc v4(false,QByteArray(20,'r')),v6(true,QByteArray(20,'r'));
    const auto directory=custom?QStringLiteral("CustomML"):QStringLiteral("mldht");
    if(custom){QVERIFY(QDir().rename(source.profile+"/plugins/mldht",source.profile+"/plugins/"+directory));source.leaf=source.profile+"/plugins/"+directory+"/baseID.config";source.identity.reset();}
    test::write(source.profile+"/downloads.config","d9:downloadslee");test::write(source.profile+"/tables.config","de");
    test::write(source.profile+"/payload-sentinel","untouched actual-owner payload sentinel");
    auto selected=policy(false);selected.mainlinePort=availablePort(false);selected.mainlineDirectory=directory.toStdString();
    selected.mainlinePeerBootstrapOnly=true;if(dual){selected.ipv6=true;selected.mainlineIpv6=true;selected.bindAddresses.push_back("::1");}
    v4.sourcePort=v6.sourcePort=static_cast<unsigned short>(selected.mainlinePort);
    const auto cache=[&](bool six,unsigned short port){QByteArray address=six?QByteArray(15,'\0')+'\1':QByteArray::fromHex("7f000001");
      address.append(char(port>>8));address.append(char(port&255));
      return dict({{"mainEntries",'l'+dict({{"addr",str(address)},{"id",str(QByteArray(20,'K'))},{"created","i800e"},{"lastSeen","i995e"},{"failedCount","i0e"},{"verified","i1e"}})+'e'},
          {"replacements","le"},{"oldKey",str(QByteArray(20,'Z'))},{"timestamp","i1000e"},{"opaque",str(QByteArray::fromHex("00ff51"))}});};
    const auto raw4=cache(false,v4.socket.localPort()),raw6=cache(true,v6.socket.localPort());
    const auto cache4=source.profile+"/plugins/"+directory+"/IPv4-table.cache",cache6=source.profile+"/plugins/"+directory+"/IPv6-table.cache";
    test::write(cache4,raw4);test::write(cache6,raw6);
    auto actual=engine::PersistentSession::create({8701,8702,8703});QVERIFY(actual);
    QVERIFY((*actual)->configureNetwork(selected));QVERIFY((*actual)->configureMainlineBootstrap({}));
    QVERIFY((*actual)->configureOriginalMainlineIdentity(*source.lease));
    // Drop all caller leases after start; the actual retained OFD must keep
    // exclusion and source lifetime, without acquiring an independent owner.
    auto caller=source.lease->retain();QVERIFY(caller);source.lease.reset();
    QVERIFY((*actual)->startIdle(**caller));caller->reset();
    auto held=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(!held);QCOMPARE(held.error().code,storage::ErrorCode::Busy);
    const auto expected=dual?std::uint64_t{2}:std::uint64_t{1};
    auto observed=[&]{auto result=(*actual)->poll();const auto state=(*actual)->mainlineDiscovery();
      return result&&state.originalIdentityActive&&state.originalIdentityAdoptions==expected&&v4.queries>0&&(!dual||v6.queries>0);};
    QTRY_VERIFY_WITH_TIMEOUT(observed(),15000);QVERIFY2(v4.failure.isEmpty(),qPrintable(v4.failure));QVERIFY2(v6.failure.isEmpty(),qPrintable(v6.failure));
    const auto state=(*actual)->mainlineDiscovery();QVERIFY(state.originalIdentityRequested);QCOMPARE(state.state,engine::MainlineDiscoveryStatus::State::Running);
    QVERIFY(!state.failure);QCOMPARE(state.originalIdentityRetirements,std::uint64_t{0});QVERIFY((*actual)->tcpListenPort());
    const auto live=(*actual)->liveSettings();QVERIFY(live);QVERIFY(!live->dht);QVERIFY(source.unchanged());
    QCOMPARE(test::read(cache4),raw4);QCOMPARE(test::read(cache6),raw6);QCOMPARE(test::read(source.profile+"/payload-sentinel"),QByteArray("untouched actual-owner payload sentinel"));
    const auto closed=(*actual)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    const auto retired=(*actual)->mainlineDiscovery();QVERIFY(retired.originalIdentityJoined);QVERIFY(!retired.originalIdentityActive);
    QCOMPARE(retired.originalIdentityAdoptions,expected);QCOMPARE(retired.originalIdentityRetirements,expected);QVERIFY(source.unchanged());
    auto released=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(released);QCOMPARE(test::read(cache4),raw4);QCOMPARE(test::read(cache6),raw6);
  }
  void persistentOwnerFailedRestoreCannotSilentlyUseANativeIdentity_data(){
    QTest::addColumn<int>("failure");QTest::newRow("missing-required-plugin-directory")<<0;QTest::newRow("malformed-original-root")<<1;QTest::newRow("replaced-after-capture")<<2;
  }
  void persistentOwnerFailedRestoreCannotSilentlyUseANativeIdentity(){
    QFETCH(int,failure);Source source(QByteArray(20,'r'));test::write(source.profile+"/downloads.config","d9:downloadslee");
    auto selected=policy(false);selected.mainlinePort=availablePort(false);selected.mainlinePeerBootstrapOnly=true;
    auto actual=engine::PersistentSession::create({8711,8712,8713});QVERIFY(actual);QVERIFY((*actual)->configureNetwork(selected));QVERIFY((*actual)->configureMainlineBootstrap({}));
    if(failure==0){QVERIFY(QFile::remove(source.leaf));QVERIFY(QDir().rmdir(QFileInfo(source.leaf).absolutePath()));}
    else if(failure==1){test::write(source.leaf,"unknown invalid original key\n");}
    const auto configured=(*actual)->configureOriginalMainlineIdentity(*source.lease);QCOMPARE(configured.has_value(),failure==2);
    if(failure==2){QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".before"));test::write(source.leaf,source.bytes);}
    QVERIFY((*actual)->startIdle(*source.lease));QTRY_VERIFY_WITH_TIMEOUT(((*actual)->poll().has_value()&&(*actual)->tcpListenPort().has_value()),5000);
    const auto denied=(*actual)->mainlineDiscovery();QVERIFY(denied.originalIdentityRequested);QVERIFY(denied.failure);
    QCOMPARE(denied.state,engine::MainlineDiscoveryStatus::State::Unavailable);QVERIFY(!denied.originalIdentityActive);QCOMPARE(denied.originalIdentityAdoptions,std::uint64_t{0});
    const auto live=(*actual)->liveSettings();QVERIFY(live);QVERIFY(!live->dht);QCOMPARE(live->proxyType,static_cast<int>(lt::settings_pack::none));
    if(failure==0){QVERIFY(!QFile::exists(source.leaf));}else{QCOMPARE(test::read(source.leaf),failure==1?QByteArray("unknown invalid original key\n"):source.bytes);}
    const auto closed=(*actual)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
  }
  void originalOptInSealsTheFinalPolicyBeforeAnySessionStarts_data(){
    QTest::addColumn<bool>("enabled");QTest::newRow("imported-source")<<true;QTest::newRow("explicitly-disabled")<<false;
  }
  void originalOptInSealsTheFinalPolicyBeforeAnySessionStarts(){
    QFETCH(bool,enabled);Source source(QByteArray(20,'r'));test::write(source.profile+"/downloads.config","d9:downloadslee");
    auto selected=policy(false);selected.dht=enabled;selected.mainlinePort=availablePort(false);selected.mainlinePeerBootstrapOnly=true;
    auto actual=engine::PersistentSession::create({8731,8732,8733});QVERIFY(actual);
    QVERIFY((*actual)->configureNetwork(selected));QVERIFY((*actual)->configureMainlineBootstrap({}));
    QVERIFY((*actual)->configureOriginalMainlineIdentity(*source.lease));
    auto changed=selected;changed.mainlineDirectory="another-original-directory";changed.mainlineRestoreIdentity=false;changed.dht=!enabled;
    const auto reconfigured=(*actual)->configureNetwork(changed);QVERIFY(!reconfigured);QCOMPARE(reconfigured.error().code,storage::ErrorCode::Conflict);
    const auto second=(*actual)->configureOriginalMainlineIdentity(*source.lease);QVERIFY(!second);QCOMPARE(second.error().code,storage::ErrorCode::Conflict);
    QVERIFY((*actual)->startIdle(*source.lease));
    QTRY_VERIFY_WITH_TIMEOUT(((*actual)->poll().has_value()&&(*actual)->tcpListenPort().has_value()
        &&(!enabled||(*actual)->mainlineDiscovery().originalIdentityActive)),5000);
    const auto observed=(*actual)->mainlineDiscovery();QCOMPARE(observed.originalIdentityRequested,enabled);QVERIFY(!observed.failure);
    QCOMPARE(observed.originalIdentityActive,enabled);QVERIFY(source.unchanged());
    const auto closed=(*actual)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
  }
  void persistentOwnerOriginalSourceLossJoinsDhtWhilePayloadOwnerRemainsUsable(){
    Source source(QByteArray(20,'r'));test::write(source.profile+"/downloads.config","d9:downloadslee");
    auto selected=policy(false);selected.mainlinePort=availablePort(false);selected.mainlinePeerBootstrapOnly=true;
    auto actual=engine::PersistentSession::create({8721,8722,8723});QVERIFY(actual);QVERIFY((*actual)->configureNetwork(selected));
    QVERIFY((*actual)->configureMainlineBootstrap({}));QVERIFY((*actual)->configureOriginalMainlineIdentity(*source.lease));QVERIFY((*actual)->startIdle(*source.lease));
    auto active=[&]{return (*actual)->poll().has_value()&&(*actual)->mainlineDiscovery().originalIdentityActive;};QTRY_VERIFY_WITH_TIMEOUT(active(),5000);
    QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".original"));test::write(source.leaf,source.bytes);
    QVERIFY((*actual)->poll());const auto lost=(*actual)->mainlineDiscovery();QCOMPARE(lost.state,engine::MainlineDiscoveryStatus::State::Unavailable);
    QVERIFY(lost.failure&&lost.originalIdentityJoined);QVERIFY(!lost.originalIdentityActive);QCOMPARE(lost.originalIdentityAdoptions,std::uint64_t{1});QCOMPARE(lost.originalIdentityRetirements,std::uint64_t{1});
    QVERIFY((*actual)->tcpListenPort());QVERIFY((*actual)->poll());QCOMPARE(test::read(source.leaf),source.bytes);
    QVERIFY((*actual)->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
#else
  void stockSdkRefusesRequestedOriginalPolicyExplicitly(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);
    auto result=engine::MainlineDhtService::create(policy(false),opts);QVERIFY(!result);
    QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);QVERIFY(source.unchanged());
  }
#endif
};
QTEST_GUILESS_MAIN(OriginalMainlineWireTests)
#include "mainline_original_identity_tests.moc"
