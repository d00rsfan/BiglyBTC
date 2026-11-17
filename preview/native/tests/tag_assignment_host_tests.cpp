// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <QElapsedTimer>
#include <functional>
using namespace biglybtc;
namespace {
bool waitFor(const std::function<bool()>& predicate,int milliseconds=15000){QElapsedTimer elapsed;elapsed.start();while(!predicate()){if(elapsed.elapsed()>=milliseconds){return false;}QTest::qWait(10);}return true;}
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto value:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(value);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}if(bits){out+=chars[(buffer<<(5-bits))&31];}return out;}
void prepare(test::OwnedEngineFixture& fixture,bool member,bool resume=true){
 auto inventory=core::Document::parse(test::read(fixture.profile+"/downloads.config").toStdString());test::require(bool(inventory),"Generated host inventory");const auto& first=inventory->find("downloads")->children.front();auto row=core::Document::parse(inventory->originalBytes().substr(first.begin,first.end-first.begin));test::require(row&&row->setInteger("state",70)&&inventory->replaceValue("downloads","l"+row->serialize()+"e"),"Generated stopped intent");test::write(fixture.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
 if(resume){
  auto state=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(bool(state),"Generated normal resume metainfo");
  const auto count=state->find("info")->find("pieces")->bytes.size()/20;
  libtorrent::entry data(libtorrent::entry::dictionary_type{});data["data"]["valid"]=1;data["data"]["resume data"]=std::string(count,'\0');data["data"]["blocks"]=libtorrent::entry::dictionary_type{};
  const auto& attributes=*state->find("attributes");auto current=core::Document::parse(state->originalBytes().substr(attributes.begin,attributes.end-attributes.begin));
  test::require(current&&current->replaceValue("resumecomplete","i1e")&&state->replaceValue("attributes",current->serialize())&&state->replaceValue("resume",test::encoded(data)),"Generated valid missing-piece map");
  test::write(fixture.stateFile,QByteArray::fromStdString(state->serialize()));
 }
 libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;settings["TCP.Listen.Port"]=6881;settings["UDP.Listen.Port"]=6881;settings["IPV6 Enable Support"]=0;settings["locale"]="en_GB";settings["tagmanager.enable"]=1;
 for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}
 settings["max downloads"]=1;settings["max active torrents"]=1;settings["min downloads"]=0;
 test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
 libtorrent::entry tags(libtorrent::entry::dictionary_type{});int id=0;for(int action:{8,4,64,128,32768}){auto& record=tags["3"][std::to_string(++id)];record["n"]="Generated action "+std::to_string(id);record["c"]["pp."+std::to_string(action)]=1;record["o"]=libtorrent::entry::list_type{};if(id==1&&member){record["o"].list().emplace_back(base32(*fixture.hash));}}
 test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));
}
core::SavedDownload saved(const test::OwnedEngineFixture& fixture){auto doc=core::Document::parse(test::read(fixture.profile+"/downloads.config").toStdString());test::require(bool(doc),"Current generated inventory");auto library=core::readLibrary(*doc);test::require(library&&library->downloads.size()==1,"Current generated library");return library->downloads.front();}
struct Harness{
 app::SessionBridge bridge;ui::Presentation rows;
 QSignalSpy sessions{&bridge,&app::SessionBridge::columnLayoutSessionReady},finished{&bridge,&app::SessionBridge::manualTagCommandFinished},closed{&bridge,&app::SessionBridge::sessionClosed};
 quint64 request=0;
 Harness(){QObject::connect(&bridge,&app::SessionBridge::snapshotReady,&bridge,[this](ui::Presentation value){rows=std::move(value);});}
 quint64 session()const{return sessions.empty()?0:sessions.last()[0].toULongLong();}
 ui::ManualTagCommandPresentation result()const{return finished.empty()?ui::ManualTagCommandPresentation{}:qvariant_cast<ui::ManualTagCommandPresentation>(finished.last()[0]);}
 bool open(test::OwnedEngineFixture& fixture,bool normal){return (normal?bridge.openNormal(fixture.native()):bridge.open(fixture.native(),0))&&waitFor([&]{return !bridge.busy()&&session()&&rows&&!rows->items.empty();});}
 ui::ManualTagCommandPresentation reload(){ui::ManualTagCommand value;value.session=session();value.request=++request;value.kind=ui::ManualTagCommandKind::Reload;const auto prior=finished.count();if(!waitFor([&]{return !bridge.busy();})||!bridge.manualTagCommand(value)||!waitFor([&]{return !bridge.busy()&&finished.count()>prior;})){return {};}return result();}
 ui::ManualTagCommandPresentation change(int id,bool add){auto source=reload();if(!source){return {};}
  ui::ManualTagCommand value;value.session=session();value.request=++request;value.generation=source->generation;value.tagId=id;value.kind=add?ui::ManualTagCommandKind::AddMember:ui::ManualTagCommandKind::RemoveMember;value.member={rows->items.front().hash,rows->revision};const auto prior=finished.count();
  if(!bridge.manualTagCommand(value)||!waitFor([&]{return !bridge.busy()&&finished.count()>prior;})){return {};}return result();
 }
 bool close(){return waitFor([&]{return !bridge.busy();})&&bridge.closeSession()&&waitFor([&]{return !closed.empty()&&!bridge.busy();},20000);}
};
}
class TagAssignmentHostTests final:public QObject {
 Q_OBJECT
private slots:
 void actualNormalOwnerDispatchesCommands(){
  test::OwnedEngineFixture fixture(false);prepare(fixture,false);const auto payload=test::read(fixture.payload+"/sample.bin");Harness host;QVERIFY(host.open(fixture,true));
  auto forced=host.change(1,true);QVERIFY(forced);QCOMPARE(forced->status,ui::ManualTagCommandStatus::Saved);QVERIFY2(forced->constraintStatus.isEmpty(),qPrintable(forced->constraintStatus));
  QVERIFY(waitFor([&]{return saved(fixture).forceStart&&host.rows&&host.rows->items.front().providerAdmission;}));
  QVERIFY(waitFor([&]{const auto state=host.rows->items.front().state;return !state.startsWith("Checking")&&!state.startsWith("Stopping")&&!state.startsWith("Waiting");}));
  auto paused=host.change(3,true);QVERIFY(paused);QCOMPARE(paused->status,ui::ManualTagCommandStatus::Saved);QVERIFY2(paused->constraintStatus.isEmpty(),qPrintable(paused->constraintStatus));
  QVERIFY(waitFor([&]{return host.rows->items.front().state.startsWith("Paused");}));
  auto resumed=host.change(4,true);QVERIFY(resumed);QCOMPARE(resumed->status,ui::ManualTagCommandStatus::Saved);QVERIFY2(resumed->constraintStatus.isEmpty(),qPrintable(resumed->constraintStatus));
  QVERIFY(waitFor([&]{return host.rows->items.front().providerAdmission&&!host.rows->items.front().state.startsWith("Paused");}));
  const auto admission=host.rows->items.front().providerAdmission;
  auto queued=host.change(5,true);QVERIFY(queued);QCOMPARE(queued->status,ui::ManualTagCommandStatus::Saved);QVERIFY2(queued->constraintStatus.isEmpty(),qPrintable(queued->constraintStatus));
  QVERIFY2(waitFor([&]{return saved(fixture).originalState==75&&host.rows->items.front().providerAdmission!=admission;},20000),qPrintable(QString("state=%1 force=%2 old=%3 current=%4 row=%5").arg(saved(fixture).originalState).arg(saved(fixture).forceStart).arg(admission).arg(host.rows->items.front().providerAdmission).arg(host.rows->items.front().state)));
  QVERIFY(waitFor([&]{return !host.rows->items.front().state.startsWith("Checking")&&!host.rows->items.front().state.startsWith("Stopping");}));
  auto stopped=host.change(2,true);QVERIFY(stopped);QVERIFY2(stopped->status==ui::ManualTagCommandStatus::Saved,qPrintable(stopped->failure));QVERIFY2(stopped->constraintStatus.isEmpty(),qPrintable(stopped->constraintStatus));
  QVERIFY(waitFor([&]{return saved(fixture).originalState==70&&!host.rows->items.front().providerAdmission;},20000));
  QVERIFY(host.close());QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
 }
 void loadReloadAndNoopNeverReplay_data(){QTest::addColumn<bool>("normal");QTest::newRow("normal-original-membership")<<true;QTest::newRow("offline-original-membership")<<false;}
 void loadReloadAndNoopNeverReplay(){QFETCH(bool,normal);test::OwnedEngineFixture fixture(false);prepare(fixture,true);const auto inventory=test::read(fixture.profile+"/downloads.config"),payload=test::read(fixture.payload+"/sample.bin");Harness host;QVERIFY(host.open(fixture,normal));QVERIFY(host.reload());QTest::qWait(200);QVERIFY(!saved(fixture).forceStart);QCOMPARE(saved(fixture).originalState,std::int64_t{70});
  if(normal){auto noop=host.change(1,true);QVERIFY(noop);QCOMPARE(noop->status,ui::ManualTagCommandStatus::Unchanged);QVERIFY(!saved(fixture).forceStart);auto removed=host.change(1,false);QVERIFY(removed);QCOMPARE(removed->status,ui::ManualTagCommandStatus::Saved);QVERIFY(!saved(fixture).forceStart);}
  QVERIFY(host.close());QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);
 }
 void missingResumeReportsCommittedAssignmentWithoutAutomaticRecheck(){
  test::OwnedEngineFixture fixture(false);prepare(fixture,false,false);const auto payload=test::read(fixture.payload+"/sample.bin");Harness host;QVERIFY(host.open(fixture,true));
  auto result=host.change(1,true);QVERIFY(result);QCOMPARE(result->status,ui::ManualTagCommandStatus::Saved);QVERIFY(result->constraintStatus.contains("resume",Qt::CaseInsensitive));
  QVERIFY(!result->tags->tags.front().uniqueHashes.empty());
  const auto errorIntent=saved(fixture);auto ignoredStop=host.change(2,true);QVERIFY(ignoredStop);QCOMPARE(ignoredStop->status,ui::ManualTagCommandStatus::Saved);QVERIFY(ignoredStop->constraintStatus.isEmpty());QCOMPARE(saved(fixture).originalState,errorIntent.originalState);QCOMPARE(saved(fixture).forceStart,errorIntent.forceStart);
  QVERIFY(host.reload());QTest::qWait(200);QVERIFY(!host.rows->items.front().state.startsWith("Checking"));QVERIFY(host.close());QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);
 }
 void offlineCannotActivateAssignmentCommands(){test::OwnedEngineFixture fixture(false);prepare(fixture,false);const auto tags=test::read(fixture.profile+"/tag.config"),inventory=test::read(fixture.profile+"/downloads.config");Harness host;QVERIFY(host.open(fixture,false));auto denied=host.change(1,true);QVERIFY(denied);QCOMPARE(denied->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(fixture.profile+"/tag.config"),tags);QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QVERIFY(host.close());}
};
QTEST_GUILESS_MAIN(TagAssignmentHostTests)
#include "tag_assignment_host_tests.moc"
