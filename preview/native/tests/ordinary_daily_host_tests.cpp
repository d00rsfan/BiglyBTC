// SPDX-License-Identifier: GPL-2.0-or-later
#include "ordinary_daily_fixture.hpp"
#include <QApplication>
#include <chrono>
#include <zlib.h>
#include <libtorrent/bdecode.hpp>
namespace {
using namespace biglybtc;using namespace biglybtc::test::daily;
using Action=core::DownloadAction;
std::int64_t value(const ui::OverallPresentation& p,Field field){test::require(bool(p),"Actual Overall packet");return p->values[static_cast<std::size_t>(field)];}
bool joinedFinal(const Host& host){return closedAndReleased(host)&&host.sample()&&host.sample()->sample&&host.sample()->state==ui::NativeSessionState::Closed&&host.totals()&&host.totals()->closed&&host.totals()->finalCounters;}
bool command(Host& host,const Profile& profile,Action action){
 host.commandFailure.clear();const auto fail=[&](const char* stage){const auto row=host.row(profile);host.commandFailure=QStringLiteral("control action=%1 stage=%2 busy=%3 mask=%4 stopped=%5 paused=%6 forced=%7").arg(static_cast<unsigned>(action)).arg(QString::fromLatin1(stage)).arg(host.bridge.busy()?1:0).arg(row?row->actionMask:0).arg(stopped(profile)?1:0).arg(paused(profile)?1:0).arg(forced(profile)?1:0);return false;};
 QElapsedTimer eligible;eligible.start();for(;;){QCoreApplication::processEvents();const auto row=host.row(profile);if(!host.bridge.busy()&&row&&(row->actionMask&ui::downloadActionBit(action))){break;}if(eligible.elapsed()>=10000){return fail("current-action-unavailable");}QTest::qWait(10);}
 const auto revision=host.snapshot()->revision;if(!host.bridge.downloadAction({host.target(profile)},action)){return fail("dispatch-refused");}
 QElapsedTimer timer;timer.start();while(timer.elapsed()<10000){QCoreApplication::processEvents();if(!host.bridge.busy()&&host.snapshot()&&host.snapshot()->revision>revision){bool acknowledged=false;switch(action){case Action::ForceStart:acknowledged=forced(profile);break;case Action::CancelForceStart:acknowledged=!forced(profile);break;case Action::Pause:acknowledged=paused(profile);break;case Action::Resume:case Action::Start:acknowledged=!paused(profile)&&!stopped(profile);break;case Action::Stop:acknowledged=stopped(profile)&&!paused(profile);break;default:acknowledged=true;break;}return acknowledged?true:fail("returned-without-saved-intent");}QTest::qWait(10);}return fail("no-command-completion");
}
void assertFinalTotals(const Host& host,const Profile& p,std::int64_t down,std::int64_t up){
 const auto sample=host.sample();test::require(sample&&sample->sample,"Actual joined final counters");
 QCOMPARE(statistic(p.path,Field::Downloaded),down+sample->sample->counters.received);
 QCOMPARE(statistic(p.path,Field::Uploaded),up+sample->sample->counters.sent);
 const auto before=core::readSavedStatistics(initialStatistics().toStdString()),after=core::readSavedStatistics(test::read(p.path+"/BiglyBT.statistics").toStdString());QVERIFY(before&&after);
 QCOMPARE(raw(*before->source,before->source->find("opaque.root")),raw(*after->source,after->source->find("opaque.root")));
 QCOMPARE(raw(*before->source,before->source->find("all")->find("opaque.all")),raw(*after->source,after->source->find("all")->find("opaque.all")));
}
}
class OrdinaryDailyHostTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
 void largeCachedLibraryPublishesRowsAndCountProgressBeforeStartingTransfers(){
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-large-library-XXXXXX"));QVERIFY(root.isValid());
  Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"large",false,true,trackers.url(false),70);
  const auto decode=[](const QByteArray& bytes){lt::bdecode_node node;lt::error_code error;lt::bdecode(bytes.constData(),bytes.constData()+bytes.size(),node,error);test::require(!error,"Large generated document");return lt::entry(node);};
  auto state=decode(test::read(profile.state)),inventory=decode(test::read(profile.path+"/downloads.config"));
  const auto base=inventory["downloads"].list().front();inventory["downloads"].list().clear();
  lt::entry cache(lt::entry::dictionary_type{});cache["state"]=lt::entry::list_type{};
  std::vector<QString> hashes;constexpr int Count=5000;
  for(int index=0;index<Count;++index){
   auto item=state;item["info"]["name"]="generated-"+std::to_string(index)+".bin";
   const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(item["info"])),QCryptographicHash::Sha1);
   const auto hex=QString::fromLatin1(digest.toHex().toUpper());hashes.push_back(hex);
   test::write(profile.path+"/active/"+hex+".dat",QByteArray::fromStdString(test::encoded(item)));
   auto row=base;row["torrent_hash"]=digest.toStdString();row["position"]=index+1;inventory["downloads"].list().push_back(row);
   lt::entry cached(lt::entry::dictionary_type{});cached["hash"]=digest.toStdString();cached["name"]=item["info"]["name"];cached["size"]=Size;cached["fc"]=1;
   cached["attributes"]["networks"]=lt::entry::list_type{lt::entry(index==0?"I2P":"Public")};cached["opaque.padding"]=std::string(512,'x');
   if(index==1){cached["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("I2P"),lt::entry("Tor")};}
   if(index==2){cached["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("Public")};}
   if(index==3){cached.dict().erase("attributes");}
   if(index==4){cached["attributes"]["networks"]=7;}
   if(index==5){auto first=cached;first["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};cache["state"].list().push_back(first);}
   cache["state"].list().push_back(cached);
  }
  test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
  const auto rawCache=test::encoded(cache);z_stream stream{};QCOMPARE(deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY),Z_OK);
  QByteArray compressed(static_cast<qsizetype>(compressBound(rawCache.size())+64),'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(rawCache.data()));stream.avail_in=rawCache.size();stream.next_out=reinterpret_cast<Bytef*>(compressed.data());stream.avail_out=compressed.size();QCOMPARE(deflate(&stream,Z_FINISH),Z_STREAM_END);compressed.resize(stream.total_out);QCOMPARE(deflateEnd(&stream),Z_OK);test::write(profile.path+"/active/cache.dat",compressed);
  auto config=document(profile.path+"/biglybt.config");for(const auto* key:{"PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.Tracker"}){QVERIFY(config.replaceValue(key,"i0e"));}test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(config.serialize()));
  Host host({});bool earlyRowsWhileBusy=false;QElapsedTimer startup;startup.start();qint64 firstRows=-1;
  QObject::connect(&host.bridge,&app::SessionBridge::snapshotReady,&host.bridge,[&](const ui::Presentation& rows){if(firstRows<0&&rows&&rows->items.size()==Count){firstRows=startup.elapsed();earlyRowsWhileBusy=host.bridge.busy();}});
  profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));
  QTRY_VERIFY2_WITH_TIMEOUT(firstRows>=0,qPrintable(host.diagnostic()),20000);QVERIFY(earlyRowsWhileBusy);
  QTRY_VERIFY2_WITH_TIMEOUT(!host.bridge.busy()&&host.snapshot()&&host.snapshot()->items.size()==Count&&host.sample()&&host.sample()->sample,qPrintable(host.diagnostic()),30000);
  bool counted=false;for(const auto& message:host.messages){if(message[0].toString().contains("5000 / 5000")){counted=true;}}QVERIFY(counted);
  const auto find=[&](int index)->ui::LibraryItem{for(const auto& row:host.snapshot()->items){if(row.hash==hashes[index]){return row;}}test::require(false,"Actual indexed large-library row");return {};};
  QVERIFY(!(find(0).actionMask&ui::downloadActionBit(Action::Start)));QVERIFY(find(2).networkStatus.contains("not supported"));QVERIFY(find(4).networkStatus.contains("not supported"));
  QVERIFY(!find(1).networkStatus.contains("not supported"));QVERIFY(!find(3).networkStatus.contains("not supported"));QVERIFY(!find(5).networkStatus.contains("not supported"));
  QVERIFY(find(1).actionMask&ui::downloadActionBit(Action::Start));QCOMPARE(host.sample()->sample->counters.payloadSent,std::int64_t{0});
  qInfo("Large library: %d rows; first presentation %lld ms; ready %lld ms",Count,static_cast<long long>(firstRows),static_cast<long long>(startup.elapsed()));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);
  QCOMPARE(test::read(profile.path+"/active/cache.dat"),compressed);QCOMPARE(test::read(profile.data),profile.body);
 }
 void normalRestoredSeedAndPartialUseActualDiscoveryAndTrackers_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4-cold-warm")<<false;QTest::newRow("IPv6-cold-warm")<<true;}
 void normalRestoredSeedAndPartialUseActualDiscoveryAndTrackers(){
  QFETCH(bool,v6);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-daily-host-XXXXXX"));QVERIFY(root.isValid());const auto address=v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;Trackers trackers(address);
  Profile seed(root.path(),"seed",v6,true,trackers.url(false),75,true),partial(root.path(),"partial",v6,false,trackers.url(true));trackers.allow(seed,partial);QVERIFY(noPortMapping(seed)&&noPortMapping(partial));
  test::mainline::OwnedKrpc router4(seed.peerPort,QHostAddress::LocalHost,QByteArray(20,'4')),router6(seed.peerPort,QHostAddress::LocalHostIPv6,QByteArray(20,'6'));QVERIFY(router4.routerId()!=router6.routerId());for(auto* router:{&router4,&router6}){router->allow(seed.wireHash());router->allowAnnouncePort(seed.peerPort);}auto& router=v6?router6:router4;router.allowAnnouncePort(partial.peerPort);
  const std::vector<std::pair<std::string,int>> bootstrap{{"127.0.0.1",router4.port()},{"::1",router6.port()}},partialBootstrap{{v6?"::1":"127.0.0.1",router.port()}};
  const auto seedConfig=test::read(seed.path+"/biglybt.config"),partialConfig=test::read(partial.path+"/biglybt.config");const auto rootIdentity=test::read(seed.path+"/plugins/mlDHT/baseID.config");
  QCOMPARE(seedSeconds(seed),seed.preexistingSeedSeconds);QVERIFY(!forced(seed));QVERIFY(!QFileInfo::exists(seed.checkpoint())&&!QFileInfo::exists(partial.checkpoint()));QCOMPARE(test::read(partial.data).first(Size/2),partial.body.first(Size/2));QVERIFY(test::read(partial.data)!=partial.body);
  Host seedHost(bootstrap),partialHost(partialBootstrap);seed.releasePorts();QVERIFY(seedHost.bridge.openNormal(seed.native()));
  const auto seeded=[&]{return seedHost.ready(seed)&&seedHost.row(seed)->state.contains("Seeding")&&router4.announced(seed.wireHash())>0&&router6.announced(seed.wireHash())>0;};
  QTRY_VERIFY2_WITH_TIMEOUT(seeded()||!router4.failure.isEmpty()||!router6.failure.isEmpty(),qPrintable(seedHost.diagnostic()+router4.failure+router6.failure+router4.issuedEvidence()+router6.issuedEvidence()),30000);
  QVERIFY2(seeded()&&router4.failure.isEmpty()&&router6.failure.isEmpty(),qPrintable(seedHost.diagnostic()+router4.failure+router6.failure+router4.issuedEvidence()+router6.issuedEvidence()));
  QVERIFY2(router4.failure.isEmpty()&&router6.failure.isEmpty(),qPrintable(router4.failure+router6.failure));for(const auto family:{QHostAddress::LocalHost,QHostAddress::LocalHostIPv6}){QUdpSocket probe;QVERIFY(!probe.bind(QHostAddress(family),seed.mainlinePort,QUdpSocket::DontShareAddress));}QVERIFY(seedHost.diagnostic().contains("Public network mode"));QVERIFY(unknownAndNetworksPreserved(seed));QVERIFY(seedHost.diagnostic().contains("I2P")&&seedHost.diagnostic().contains("Tor")&&seedHost.diagnostic().contains("inactive"));
  partial.releasePorts();QVERIFY(partialHost.bridge.openNormal(partial.native()));bool sawIncoming=false;
  const auto transferred=[&]{sawIncoming=sawIncoming||incoming(v6,seed.peerPort);const auto a=seedHost.sample(),b=partialHost.sample();return partialHost.ready(partial)&&partialHost.row(partial)->completed==Size&&a&&a->sample&&b&&b->sample&&a->sample->counters.payloadSent>0&&b->sample->counters.payloadReceived>0&&router.gets(seed.wireHash())>=2&&router.announced(seed.wireHash())>=2&&trackers.httpAnnounces>0&&trackers.udpAnnounces>0;};
  QTRY_VERIFY2_WITH_TIMEOUT(transferred(),qPrintable(seedHost.diagnostic()+partialHost.diagnostic()+router.failure+trackers.failure),40000);
  QVERIFY(sawIncoming);QVERIFY2(router.failure.isEmpty(),qPrintable(router.failure));QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));
  QCOMPARE(test::read(seed.data),seed.body);QCOMPARE(test::read(partial.data),partial.body);QCOMPARE(QCryptographicHash::hash(test::read(partial.data),QCryptographicHash::Sha1),QCryptographicHash::hash(seed.body,QCryptographicHash::Sha1));
  const auto clients=router.clientIds(seed.wireHash());QVERIFY(clients.contains(seed.key)&&clients.contains(partial.key));QVERIFY(router4.clientIds(seed.wireHash()).contains(seed.key)&&router6.clientIds(seed.wireHash()).contains(seed.key));QVERIFY(router4.sourcePorts.contains(seed.mainlinePort)&&router6.sourcePorts.contains(seed.mainlinePort));QVERIFY(router.sourcePorts.contains(seed.mainlinePort)&&router.sourcePorts.contains(partial.mainlinePort));
  QVERIFY(seedHost.bridge.closeSession());QVERIFY(partialHost.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(seedHost)&&joinedFinal(partialHost),15000);
  assertFinalTotals(seedHost,seed,OldDown,OldUp);assertFinalTotals(partialHost,partial,OldDown,OldUp);QVERIFY(unknownAndNetworksPreserved(seed)&&unknownAndNetworksPreserved(partial));for(const auto family:{QHostAddress::LocalHost,QHostAddress::LocalHostIPv6}){QUdpSocket released;QVERIFY(released.bind(QHostAddress(family),seed.mainlinePort,QUdpSocket::DontShareAddress));}
  QVERIFY(QFileInfo::exists(seed.checkpoint())&&QFileInfo::exists(partial.checkpoint()));QCOMPARE(test::read(seed.path+"/biglybt.config"),seedConfig);QCOMPARE(test::read(partial.path+"/biglybt.config"),partialConfig);QVERIFY(!QFileInfo::exists(seed.path+"/plugins/mldht"));
  const auto genuineWarmSeconds=seedSeconds(seed);QVERIFY(genuineWarmSeconds>seed.preexistingSeedSeconds);
  const auto beforeGets4=router4.gets(seed.wireHash()),beforeGets6=router6.gets(seed.wireHash()),beforeAnnounce4=router4.announced(seed.wireHash()),beforeAnnounce6=router6.announced(seed.wireHash());const auto savedDown=statistic(seed.path,Field::Downloaded),savedUp=statistic(seed.path,Field::Uploaded);
  Host warm(bootstrap);QVERIFY(warm.bridge.openNormal(seed.native()));QTRY_VERIFY2_WITH_TIMEOUT(warm.ready(seed)&&router4.gets(seed.wireHash())>beforeGets4&&router6.gets(seed.wireHash())>beforeGets6&&router4.announced(seed.wireHash())>beforeAnnounce4&&router6.announced(seed.wireHash())>beforeAnnounce6,qPrintable(warm.diagnostic()+router4.failure+router6.failure),30000);
  QCOMPARE(warm.sample()->sample->counters.payloadReceived,std::int64_t{0});QCOMPARE(test::read(seed.path+"/plugins/mlDHT/baseID.config"),rootIdentity);QVERIFY(warm.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(warm),15000);assertFinalTotals(warm,seed,savedDown,savedUp);QVERIFY(seedSeconds(seed)>=genuineWarmSeconds);QVERIFY2(router4.failure.isEmpty()&&router6.failure.isEmpty(),qPrintable(router4.failure+router6.failure));QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));
  auto released=storage::ProfileLease::acquire(seed.native());QVERIFY(released);QVERIFY(unknownAndNetworksPreserved(seed));
 }
 void normalStopThenStartWaitsActualActionEligibility(){
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-daily-control-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"control",false,true,trackers.url(false),70);trackers.allow(profile,profile);
  auto config=document(profile.path+"/biglybt.config");QVERIFY(config.replaceValue("Peer Source Selection Default.DHT","i0e"));test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(config.serialize()));
  Host host({});profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile),qPrintable(host.diagnostic()),15000);
  QVERIFY2(command(host,profile,Action::Start),qPrintable(host.diagnostic()));QTRY_VERIFY2_WITH_TIMEOUT(host.row(profile)&&host.row(profile)->state.contains("Seeding"),qPrintable(host.diagnostic()),15000);
  QVERIFY2(command(host,profile,Action::Stop),qPrintable(host.diagnostic()));QVERIFY(stopped(profile));
  QVERIFY2(command(host,profile,Action::Start),qPrintable(host.diagnostic()));QVERIFY(!stopped(profile));QTRY_VERIFY2_WITH_TIMEOUT(host.row(profile)&&host.row(profile)->state.contains("Seeding"),qPrintable(host.diagnostic()),15000);
  QVERIFY2(command(host,profile,Action::Stop),qPrintable(host.diagnostic()));QVERIFY(stopped(profile));QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);QCOMPARE(test::read(profile.data),profile.body);QVERIFY(unknownAndNetworksPreserved(profile));
 }
 void oneActualIdleMinuteControlsAndJoinedReopenDoNotReplayLifetime(){
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-daily-accounting-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"idle",false,true,trackers.url(false),70);trackers.allow(profile,profile);QVERIFY(noPortMapping(profile));
  test::mainline::OwnedKrpc router(profile.peerPort);router.allowWithoutPeers(profile.wireHash());router.allowAnnouncePort(profile.peerPort);const std::vector<std::pair<std::string,int>> bootstrap{{"127.0.0.1",router.port()}};
  Host host(bootstrap);profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile),qPrintable(host.diagnostic()),30000);QVERIFY(stopped(profile));QCOMPARE(host.sample()->sample->counters,core::EngineCounters{});const auto session=host.sample()->session;
  QElapsedTimer idle;idle.start();QTRY_VERIFY2_WITH_TIMEOUT(idle.elapsed()>=65000&&host.sample()->ownerElapsedNanoseconds>=65000000000LL&&value(host.totals(),Field::Uptime)>=OldUptime+60,qPrintable(host.diagnostic()),75000);
  QCOMPARE(value(host.totals(),Field::Downloaded),OldDown);QCOMPARE(value(host.totals(),Field::Uploaded),OldUp);QVERIFY(host.sample()->session==session);QCOMPARE(host.sample()->sample->counters,core::EngineCounters{});
  QVERIFY2(command(host,profile,Action::ForceStart),qPrintable(host.diagnostic()));QVERIFY(forced(profile));
  QTRY_VERIFY_WITH_TIMEOUT(host.row(profile)&&host.row(profile)->state.contains("Seeding"),30000);
  QVERIFY2(command(host,profile,Action::CancelForceStart),qPrintable(host.diagnostic()));QVERIFY(!forced(profile));
  QVERIFY2(command(host,profile,Action::Pause),qPrintable(host.diagnostic()));QVERIFY(paused(profile));
  QVERIFY2(command(host,profile,Action::Resume),qPrintable(host.diagnostic()));QVERIFY(!paused(profile));
  QVERIFY2(command(host,profile,Action::Stop),qPrintable(host.diagnostic()));QVERIFY(stopped(profile)&&!forced(profile));
  QVERIFY2(command(host,profile,Action::Start),qPrintable(host.diagnostic()));QVERIFY(!stopped(profile));
  QVERIFY2(command(host,profile,Action::Stop),qPrintable(host.diagnostic()));QVERIFY(stopped(profile));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);assertFinalTotals(host,profile,OldDown,OldUp);const auto down=statistic(profile.path,Field::Downloaded),up=statistic(profile.path,Field::Uploaded),uptime=statistic(profile.path,Field::Uptime);QVERIFY(uptime>=OldUptime+60);QVERIFY(unknownAndNetworksPreserved(profile));
  Host reopened(bootstrap);QVERIFY(reopened.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(reopened.ready(profile),qPrintable(reopened.diagnostic()),30000);QVERIFY(stopped(profile)&&!paused(profile)&&!forced(profile));QCOMPARE(reopened.sample()->sample->counters,core::EngineCounters{});QCOMPARE(value(reopened.totals(),Field::Downloaded),down);QCOMPARE(value(reopened.totals(),Field::Uploaded),up);QVERIFY(reopened.sample()->session!=session);
  QVERIFY(reopened.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(reopened),15000);assertFinalTotals(reopened,profile,down,up);QCOMPARE(statistic(profile.path,Field::Downloaded),down);QCOMPARE(statistic(profile.path,Field::Uploaded),up);QVERIFY(statistic(profile.path,Field::Uptime)>=uptime);QCOMPARE(test::read(profile.data),profile.body);QVERIFY2(router.failure.isEmpty(),qPrintable(router.failure));QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));
  auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);
 }
};
QTEST_MAIN(OrdinaryDailyHostTests)
#include "ordinary_daily_host_tests.moc"
