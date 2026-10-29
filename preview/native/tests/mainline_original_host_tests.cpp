#include "owned_original_mainline.hpp"
#include "session_bridge.hpp"
#include "persistent_session.hpp"
#include "mainline_bind_plan.hpp"
#include "plugin_settings.hpp"
#include <QApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QFileInfo>

namespace {
using namespace biglybtc;
using namespace biglybtc::test::original_mainline;
unsigned short transferPort(bool dual){
  QTcpServer v4,v6;QUdpSocket u4,u6;
  test::require(v4.listen(QHostAddress::LocalHost,0),"Owned Host transfer port");const auto port=v4.serverPort();
  test::require(u4.bind(QHostAddress::LocalHost,port,QUdpSocket::DontShareAddress),"Owned Host transfer UDP port");
  if(dual){test::require(v6.listen(QHostAddress::LocalHostIPv6,port)&&u6.bind(QHostAddress::LocalHostIPv6,port,QUdpSocket::DontShareAddress),"Owned dual transfer port");}
  return port;
}
QByteArray cache(bool v6,unsigned short port){
  QByteArray address=v6?QByteArray(15,'\0')+'\1':QByteArray::fromHex("7f000001");address.append(char(port>>8));address.append(char(port&255));
  return dict({{"mainEntries",'l'+dict({{"addr",str(address)},{"id",str(QByteArray(20,'K'))},{"created","i800e"},{"lastSeen","i995e"},{"failedCount","i0e"},{"verified","i1e"}})+'e'},
      {"replacements","le"},{"oldKey",str(QByteArray(20,'Z'))},{"timestamp","i1000e"},{"opaque",str(QByteArray::fromHex("00ff51"))}});
}
void savedProfile(Source& source,bool dual,unsigned short dhtPort,unsigned short port,bool restore=true,bool enabled=true){
  test::write(source.profile+"/downloads.config","d9:downloadslee");test::write(source.profile+"/tables.config","de");
  test::write(source.profile+"/payload-sentinel","never admitted or opened for Mainline");
  const auto integer=[](auto value){return QByteArray("i")+QByteArray::number(value)+'e';};
  test::write(source.profile+"/biglybt.config",dict({{"Bind IP",str(dual?"127.0.0.1;::1":"127.0.0.1")},{"Enforce Bind IP","i1e"},
      {"IPV6 Enable Support",integer(dual?1:0)},{"TCP.Listen.Port",integer(port)},{"UDP.Listen.Port",integer(port)},
      {"PluginInfo.mldht.enabled","i1e"},{"Plugin.mldht.enable",integer(enabled?1:0)},{"Plugin.mldht.port",integer(dhtPort)},
      {"Plugin.mldht.autostart.IPv4","i1e"},{"Plugin.mldht.autostart.IPv6",integer(dual?1:0)},
      {"Plugin.mldht.onlyPeerBootstrap","i1e"},{"Plugin.mldht.alwaysRestoreID",integer(restore?1:0)},
      {"Plugin.DHT.dht.enabled","i0e"},{"Plugin.UPnP.upnp.enable","i0e"},{"Plugin.UPnP.natpmp.enable","i0e"}}));
}
void savedDefaultProfile(Source& source,unsigned short dhtPort,unsigned short peerPort){
  test::write(source.profile+"/downloads.config","d9:downloadslee");test::write(source.profile+"/tables.config","de");
  test::write(source.profile+"/payload-sentinel","never admitted or opened for Mainline");
  const auto integer=[](auto value){return QByteArray("i")+QByteArray::number(value)+'e';};
  test::write(source.profile+"/biglybt.config",dict({{"TCP.Listen.Port",integer(peerPort)},{"UDP.Listen.Port",integer(peerPort)},
      {"IPV6 Enable Support","i0e"},{"Plugin.mldht.port",integer(dhtPort)},
      {"Plugin.mldht.autostart.IPv4","i1e"},{"Plugin.mldht.autostart.IPv6","i0e"},
      {"Plugin.mldht.onlyPeerBootstrap","i1e"},{"Plugin.mldht.alwaysRestoreID","i1e"},
      {"Plugin.UPnP.upnp.enable","i0e"},{"Plugin.UPnP.natpmp.enable","i0e"},{"future-unknown",str("retained")}}));
}
engine::NetworkPolicy defaultPolicy(Source& source){
  auto file=storage::ConfigFile::load(*source.lease,"biglybt.config");test::require(file.has_value(),"Actual default bind configuration");
  auto policy=engine::readNetworkPolicy(&file->document(),{.mode=engine::NetworkMode::Normal});
  test::require(policy.has_value(),"Actual original default bind policy");return std::move(*policy);
}
bool mainlineFailure(const QSignalSpy& messages){for(const auto& message:messages){if(message[0].toString().contains("Mainline discovery is unavailable:")){return true;}}return false;}
}
class OriginalMainlineHostTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
#if __has_include(<libtorrent/biglybtc_mainline_identity.hpp>)
  void actualNormalHostLoadsOriginalRootBeforeItsOneOwnerStarts_data(){
    QTest::addColumn<bool>("dual");QTest::newRow("ipv4-original-root")<<false;
#if __has_include(<libtorrent/biglybtc_mainline_routing.hpp>)
    QTest::newRow("dual-original-same-slot-zero")<<true;
#endif
  }
  void actualNormalHostLoadsOriginalRootBeforeItsOneOwnerStarts(){
    QFETCH(bool,dual);Source source(QByteArray(20,'r'));Krpc v4(false,QByteArray(20,'r')),v6(true,QByteArray(20,'r'));
    const auto port=transferPort(dual);const auto dht=availablePort(false);QVERIFY(port!=dht);v4.sourcePort=v6.sourcePort=dht;
    const auto raw4=cache(false,v4.socket.localPort()),raw6=cache(true,v6.socket.localPort());
    const auto path4=source.profile+"/plugins/mldht/IPv4-table.cache",path6=source.profile+"/plugins/mldht/IPv6-table.cache";
    test::write(path4,raw4);test::write(path6,raw6);savedProfile(source,dual,dht,port);
    const auto originalConfig=test::read(source.profile+"/biglybt.config");source.identity.reset();source.lease.reset(); // Only Host acquires the real owner.
    app::SessionBridge bridge;QSignalSpy ready(&bridge,&app::SessionBridge::columnLayoutSessionReady),rows(&bridge,&app::SessionBridge::snapshotReady),
        samples(&bridge,&app::SessionBridge::nativeSessionReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(source.profile.toStdString()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!ready.empty()&&!rows.empty()&&!samples.empty()&&v4.queries>0&&(!dual||v6.queries>0),15000);
    QVERIFY2(v4.failure.isEmpty(),qPrintable(v4.failure));QVERIFY2(v6.failure.isEmpty(),qPrintable(v6.failure));QVERIFY(!mainlineFailure(messages));QVERIFY(source.unchanged());
    QCOMPARE(test::read(path4),raw4);QCOMPARE(test::read(path6),raw6);QCOMPARE(test::read(source.profile+"/biglybt.config"),originalConfig);
    auto busy=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(!busy);QCOMPARE(busy.error().code,storage::ErrorCode::Busy);
    // Replacement is actual namespace interference on this generated profile;
    // the current original source must fail rather than generate stock IDs.
    QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".retained"));test::write(source.leaf,source.bytes);
    QTRY_VERIFY_WITH_TIMEOUT(mainlineFailure(messages),10000);QVERIFY(!bridge.busy());QCOMPARE(test::read(source.leaf),source.bytes);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),qsizetype{1},10000);
    QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto released=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(released);
    QCOMPARE(test::read(path4),raw4);QCOMPARE(test::read(path6),raw6);QCOMPARE(test::read(source.profile+"/payload-sentinel"),QByteArray("never admitted or opened for Mainline"));
  }
  void actualDefaultOwnerCreatesRootBindsGenuineRouteAndRetires_data(){
    QTest::addColumn<bool>("invalidate");QTest::newRow("joined-default-route")<<false;
    QTest::newRow("changed-original-settings-withdraw")<<true;
  }
  void actualDefaultOwnerCreatesRootBindsGenuineRouteAndRetires(){
    QFETCH(bool,invalidate);Source source(QByteArray(20,'r'));const auto transfer=transferPort(false);
    savedDefaultProfile(source,availablePort(false),transfer);auto policy=defaultPolicy(source);
    QVERIFY(policy.mainlineOriginalBindPlan);QVERIFY(!policy.ipv6);
    auto config=storage::ConfigFile::load(*source.lease,"biglybt.config");QVERIFY(config);
    auto original=storage::readOriginalMldhtSettings(config->document(),"mldht");QVERIFY(original);
    auto options=engine::mainlineBindOptions(*original,policy.bindAddresses);QVERIFY(options);
    // Genuine non-sending OS route/interface capture: injected unit facts never
    // grant this owner permission. No KRPC/public bootstrap is requested here.
    auto capture=engine::MainlineBindCapture::capture(*source.lease,policy,*options);QVERIFY(capture);
    const auto& family=capture->plan().families[0];QCOMPARE(family.state,engine::MainlineFamilyBindState::Ready);
    QCOMPARE(family.bindings.size(),std::size_t{1});const QHostAddress local(QString::fromStdString(family.bindings[0].address));
    QVERIFY(!local.isNull()&&!local.isLoopback());QUdpSocket reservation;
    QVERIFY(reservation.bind(local,0,QUdpSocket::DontShareAddress));const auto dht=reservation.localPort();reservation.close();QVERIFY(dht!=transfer);
    savedDefaultProfile(source,dht,transfer);policy=defaultPolicy(source);const auto before=test::read(source.profile+"/biglybt.config");
    source.identity.reset();QVERIFY(QFile::remove(source.leaf));
    auto created=engine::PersistentSession::create({9911,9912,invalidate?9914U:9913U});QVERIFY(created);auto actual=std::move(*created);
    QVERIFY(actual->configureNetwork(policy));QVERIFY(actual->configureMainlineBootstrap({}));
    const auto configured=actual->configureOriginalMainlineIdentity(*source.lease);
    QVERIFY2(configured,configured?"":configured.error().message.c_str());QVERIFY(actual->startIdle(*source.lease));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->mainlineDiscovery().originalIdentityActive),15000);
    const auto active=actual->mainlineDiscovery();QCOMPARE(active.originalIdentityAdoptions,std::uint64_t{1});
    QVERIFY(active.originalIdentityRequested&&!active.originalIdentityJoined&&!active.failure);
    QUdpSocket occupied;QVERIFY(!occupied.bind(local,dht,QUdpSocket::DontShareAddress));
    const auto root=test::read(source.leaf);QCOMPARE(root.size(),qsizetype{41});
    QCOMPARE(root,QByteArray::fromHex(root.first(40)).toHex().toUpper()+'\n');
    QCOMPARE(test::read(source.profile+"/biglybt.config"),before);
    const auto payload=actual->liveSettings();QVERIFY(payload);QVERIFY(!payload->dht);
    QCOMPARE(payload->listenInterfaces,std::string("0.0.0.0:")+std::to_string(transfer));
    auto unrelated=storage::ConfigFile::load(*source.lease,"biglybt.config");QVERIFY(unrelated);
    QVERIFY(unrelated->document().replaceValue("future-unknown","7:changed"));QVERIFY(unrelated->save(*source.lease));
    QVERIFY(actual->poll());QVERIFY(actual->mainlineDiscovery().originalIdentityActive); // Unknown fields are not route authority.
    if(invalidate){
      auto changed=storage::ConfigFile::load(*source.lease,"biglybt.config");QVERIFY(changed);
      QVERIFY(changed->document().replaceValue("Plugin.mldht.multihoming","i1e"));QVERIFY(changed->save(*source.lease));
      QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->mainlineDiscovery().state==engine::MainlineDiscoveryStatus::State::Unavailable),5000);
      const auto retired=actual->mainlineDiscovery();QVERIFY(retired.failure&&retired.originalIdentityJoined&&!retired.originalIdentityActive);
      QCOMPARE(retired.originalIdentityAdoptions,retired.originalIdentityRetirements);
    }
    const auto closed=actual->close(std::chrono::steady_clock::now()+std::chrono::seconds(5));
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    const auto retired=actual->mainlineDiscovery();QVERIFY(retired.originalIdentityJoined&&!retired.originalIdentityActive);
    QCOMPARE(retired.originalIdentityAdoptions,retired.originalIdentityRetirements);
    QUdpSocket released;QVERIFY(released.bind(local,dht,QUdpSocket::DontShareAddress));
    QCOMPARE(test::read(source.leaf),root);QCOMPARE(test::read(source.profile+"/payload-sentinel"),QByteArray("never admitted or opened for Mainline"));
  }
  void defaultOwnerRefusesChangedSourceBeforeMissingRootPublication(){
    Source source(QByteArray(20,'r'));savedDefaultProfile(source,availablePort(false),transferPort(false));
    const auto captured=defaultPolicy(source);source.identity.reset();QVERIFY(QFile::remove(source.leaf));
    auto changed=storage::ConfigFile::load(*source.lease,"biglybt.config");QVERIFY(changed);
    QVERIFY(changed->document().replaceValue("Plugin.mldht.multihoming","i1e"));QVERIFY(changed->save(*source.lease));
    auto created=engine::PersistentSession::create({9921,9922,9923});QVERIFY(created);auto actual=std::move(*created);
    QVERIFY(actual->configureNetwork(captured));QVERIFY(actual->configureMainlineBootstrap({}));
    const auto configured=actual->configureOriginalMainlineIdentity(*source.lease);QVERIFY(!configured);
    QCOMPARE(configured.error().code,storage::ErrorCode::Conflict);QVERIFY(!QFileInfo::exists(source.leaf));
    QVERIFY(actual->startIdle(*source.lease));QVERIFY(actual->poll());
    QCOMPARE(actual->mainlineDiscovery().state,engine::MainlineDiscoveryStatus::State::Unavailable);
    const auto closed=actual->close(std::chrono::steady_clock::now()+std::chrono::seconds(5));
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QVERIFY(!QFileInfo::exists(source.leaf));
  }
  void malformedOriginalIsReportedWithoutDisablingPayloadOwner(){
    Source source(QByteArray(20,'r'));const auto dht=availablePort(false);const auto port=transferPort(false);QVERIFY(port!=dht);
    savedProfile(source,false,dht,port);source.identity.reset();source.lease.reset();
    test::write(source.leaf,"opaque malformed original root\n");
    app::SessionBridge bridge;QSignalSpy ready(&bridge,&app::SessionBridge::columnLayoutSessionReady),samples(&bridge,&app::SessionBridge::nativeSessionReady),
        messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(source.profile.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!ready.empty()&&!samples.empty()&&mainlineFailure(messages),10000);
    QCOMPARE(test::read(source.leaf),QByteArray("opaque malformed original root\n"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),qsizetype{1},10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
    auto released=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(released);
  }
  void missingOriginalIsCreatedOnceByTheRealHostAndReusedAfterRestart(){
    Source source(QByteArray(20,'r'));const auto dht=availablePort(false);const auto port=transferPort(false);QVERIFY(port!=dht);
    savedProfile(source,false,dht,port);source.identity.reset();source.lease.reset();QVERIFY(QFile::remove(source.leaf));
    const auto config=test::read(source.profile+"/biglybt.config");QByteArray created;
    for(int start=0;start<2;++start){
      app::SessionBridge bridge;QSignalSpy ready(&bridge,&app::SessionBridge::columnLayoutSessionReady),samples(&bridge,&app::SessionBridge::nativeSessionReady),
          messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
      QVERIFY(bridge.openNormal(source.profile.toStdString()));
      QTRY_VERIFY2_WITH_TIMEOUT(!bridge.busy()&&!ready.empty()&&!samples.empty()&&QFile::exists(source.leaf),
          qPrintable(messages.isEmpty()?QStringLiteral("Host has not published the owned missing root"):messages.last().at(0).toString()),10000);
      QVERIFY(!mainlineFailure(messages));const auto bytes=test::read(source.leaf);
      QCOMPARE(bytes.size(),qsizetype{41});const auto key=QByteArray::fromHex(bytes.first(40));QCOMPARE(key.size(),qsizetype{20});
      QCOMPARE(bytes,key.toHex().toUpper()+'\n');
      if(start==0){created=bytes;}else{QCOMPARE(bytes,created);}
      QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),qsizetype{1},10000);
      QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
      auto lease=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(lease);
      auto original=storage::OriginalMainlineIdentity::load(*lease,"mldht",true);QVERIFY(original);
      QCOMPARE(original->snapshot().state,storage::OriginalMainlineIdentityState::Imported);
      QCOMPARE(original->snapshot().originalBytes,created.toStdString());QVERIFY(original->validate(*lease));
      QCOMPARE(test::read(source.profile+"/biglybt.config"),config);
      QCOMPARE(test::read(source.profile+"/payload-sentinel"),QByteArray("never admitted or opened for Mainline"));
    }
  }
#endif
  void defaultOfflineAndDisabledMainlineNeverRequestOriginalRestore_data(){
    QTest::addColumn<bool>("normal");QTest::newRow("offline-default")<<false;QTest::newRow("normal-plugin-disabled")<<true;
  }
  void defaultOfflineAndDisabledMainlineNeverRequestOriginalRestore(){
    QFETCH(bool,normal);Source source(QByteArray(20,'r'));const auto dht=availablePort(false);const auto port=transferPort(false);QVERIFY(port!=dht);
    savedProfile(source,false,dht,port,true,false);source.identity.reset();source.lease.reset();
    QFile root(source.leaf);QVERIFY(root.rename(source.leaf+".original"));QVERIFY(QFile::link(source.leaf+".original",source.leaf));
    app::SessionBridge bridge;QSignalSpy ready(&bridge,&app::SessionBridge::columnLayoutSessionReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(normal?bridge.openNormal(source.profile.toStdString()):bridge.open(source.profile.toStdString(),0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!ready.empty(),10000);QVERIFY(!mainlineFailure(messages));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),qsizetype{1},10000);QVERIFY(QFileInfo(source.leaf).isSymLink());
    QCOMPARE(QFileInfo(source.leaf).symLinkTarget(),source.leaf+".original");QCOMPARE(test::read(source.leaf+".original"),source.bytes);
    auto released=storage::ProfileLease::acquire(source.profile.toStdString());QVERIFY(released);
  }
};
QTEST_MAIN(OriginalMainlineHostTests)
#include "mainline_original_host_tests.moc"
