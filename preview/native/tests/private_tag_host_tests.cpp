// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <sys/stat.h>
using namespace biglybtc;
namespace {
bool waitFor(const std::function<bool()>& predicate,int millis=20000){QElapsedTimer age;age.start();while(!predicate()){if(age.elapsed()>=millis){return false;}QTest::qWait(10);}return true;}
bool member(const owned_i2p_test::Fixture& f,int id){auto tags=core::readSavedDownloadTags(owned_i2p_test::read(f.path/"tag.config"));if(!tags){return false;}const auto row=std::ranges::find_if(tags->tags,[&](const auto& x){return x.id==id;});return row!=tags->tags.end()&&std::ranges::find(row->uniqueHashes,f.hash)!=row->uniqueHashes.end();}
}
class PrivateTagHostTests final:public QObject {
 Q_OBJECT
private slots:
 void realHostRateMembership_data(){QTest::addColumn<bool>("privateOnly");QTest::newRow("normal-cohort")<<false;QTest::newRow("private-only")<<true;}
 void realHostRateMembership(){
  QFETCH(bool,privateOnly);owned_i2p_test::Fixture f;
  auto config=core::Document::parse(owned_i2p_test::read(f.path/"biglybt.config"));QVERIFY(config);
  for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){QVERIFY(config->replaceValue(key,"i0e"));}
  QVERIFY(config->replaceValue("Bind IP","9:127.0.0.1"));QVERIFY(config->replaceValue("max downloads","i1e"));QVERIFY(config->replaceValue("max active torrents","i2e"));QVERIFY(config->replaceValue("StartStopManager_iRankType","i0e"));
  owned_i2p_test::write(f.path/"biglybt.config",config->serialize());
  using owned_i2p_test::dict;using owned_i2p_test::b;
  owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",dict({{"rl.down","i16384e"}})},{"n",b("Generated private rate")},{"o","le"}})}})}}));
  auto inventorySource=core::Document::parse(owned_i2p_test::read(f.path/"downloads.config"));QVERIFY(inventorySource);
  const auto& saved=inventorySource->find("downloads")->children.front();auto row=core::Document::parse(inventorySource->originalBytes().substr(saved.begin,saved.end-saved.begin));QVERIFY(row);
  QVERIFY(row->replaceValue("position","i1e"));QVERIFY(row->replaceValue("completedbytes","i0e"));QVERIFY(row->replaceValue("downloaded","i0e"));QVERIFY(row->replaceValue("uploaded","i0e"));QVERIFY(inventorySource->replaceValue("downloads","l"+row->serialize()+"e"));owned_i2p_test::write(f.path/"downloads.config",inventorySource->serialize());
  // The real Host must obtain its own exclusive lease.
  f.lease.reset();QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
  QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){server.nextPendingConnection()->setReadBufferSize(4096);}});
  app::I2pServiceOptions options;options.routerPort=server.serverPort();options.publicCoexistence=!privateOnly;
  app::SessionBridge host(nullptr,app::SessionBridge::CountryProviderFactory{},app::SessionBridge::AzureusServiceFactory{},app::SessionBridge::SchedulerFactory{},options);
  ui::Presentation rows;ui::I2pPresentation i2p;quint64 session=0;
  QObject::connect(&host,&app::SessionBridge::snapshotReady,&host,[&](ui::Presentation x){rows=std::move(x);});
  QObject::connect(&host,&app::SessionBridge::i2pReady,&host,[&](ui::I2pPresentation x){i2p=std::move(x);});
  QObject::connect(&host,&app::SessionBridge::columnLayoutSessionReady,&host,[&](quint64 x){session=x;});
  QSignalSpy commands(&host,&app::SessionBridge::manualTagCommandFinished),chips(&host,&app::SessionBridge::tagChipAssignmentFinished),closed(&host,&app::SessionBridge::sessionClosed);
  QVERIFY(host.openNormal(f.path));QVERIFY(waitFor([&]{return !host.busy()&&session&&rows&&rows->items.size()==1&&i2p&&!i2p->torrents.empty();}));
  const auto state=owned_i2p_test::read(f.state()),inventory=owned_i2p_test::read(f.path/"downloads.config");
  const auto payload=std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin";struct stat before{};QVERIFY(::stat(payload.c_str(),&before)==0);
  quint64 request=0;
  const auto reload=[&]()->ui::ManualTagCommandPresentation{
   ui::ManualTagCommand command;command.session=session;command.request=++request;command.kind=ui::ManualTagCommandKind::Reload;
   const auto count=commands.count();if(!waitFor([&]{return !host.busy();})||!host.manualTagCommand(command)||!waitFor([&]{return !host.busy()&&commands.count()>count;})){return {};}
   return qvariant_cast<ui::ManualTagCommandPresentation>(commands.last()[0]);
  };
  auto source=reload();QVERIFY(source);QVERIFY(source->rateOnlyAssignmentsAvailable);
  ui::ManualTagCommand add;add.session=session;add.request=++request;add.generation=source->generation;add.tagId=7;add.kind=ui::ManualTagCommandKind::AddMember;add.member={rows->items.front().hash,rows->revision};
  auto count=commands.count();QVERIFY(host.manualTagCommand(add));QVERIFY(waitFor([&]{return !host.busy()&&commands.count()>count;}));
  auto changed=qvariant_cast<ui::ManualTagCommandPresentation>(commands.last()[0]);QVERIFY2(changed&&changed->changed,changed?qPrintable(changed->failure):"Missing actual Host result");QVERIFY(member(f,7));
  ui::TagChipAssignmentCommand chip;chip.session=session;chip.request=++request;chip.kind=ui::TagChipAssignmentKind::Manual;chip.tagId=7;chip.targets={{rows->items.front().hash,rows->revision}};
  count=chips.count();QVERIFY(host.assignTagChip(chip));QVERIFY(waitFor([&]{return !host.busy()&&chips.count()>count;}));
  auto result=qvariant_cast<ui::TagChipAssignmentResult>(chips.last()[0]);QVERIFY2(result.failure.isEmpty(),qPrintable(result.failure));QCOMPARE(result.changed,std::size_t{1});QVERIFY(!member(f,7));
  // A real Host edit updates the prepared private source as well as settings.
  source=reload();QVERIFY(source);ui::ManualTagCommand limit;limit.session=session;limit.request=++request;limit.generation=source->generation;limit.tagId=7;limit.kind=ui::ManualTagCommandKind::SetRateLimit;limit.rateBytesPerSecond=-1;limit.uploadRate=false;
  count=commands.count();QVERIFY(host.manualTagCommand(limit));QVERIFY(waitFor([&]{return !host.busy()&&commands.count()>count;}));
  changed=qvariant_cast<ui::ManualTagCommandPresentation>(commands.last()[0]);QVERIFY2(changed&&changed->changed,changed?qPrintable(changed->failure):"Missing rate result");
  QCOMPARE(owned_i2p_test::read(f.state()),state);QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),inventory);
  struct stat after{};QVERIFY(::stat(payload.c_str(),&after)==0);QCOMPARE(after.st_ino,before.st_ino);QCOMPARE(after.st_size,before.st_size);
  QVERIFY(host.closeSession());QVERIFY(waitFor([&]{return !host.busy()&&!closed.empty();}));QVERIFY(closed.last()[1].toBool());QVERIFY(closed.last()[2].toBool());
 }
};
QTEST_GUILESS_MAIN(PrivateTagHostTests)
#include "private_tag_host_tests.moc"
