// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "manual_tag_editor.hpp"
#include "owned_engine_fixture.hpp"
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
using namespace biglybtc;
namespace {
ui::ManualTagCommandPresentation latest(const QSignalSpy& spy){return spy.empty()?ui::ManualTagCommandPresentation{}:qvariant_cast<ui::ManualTagCommandPresentation>(spy.last()[0]);}
std::size_t count(const QString& path,int id){auto source=core::readSavedDownloadTags(test::read(path).toStdString());if(!source){return 999;}for(const auto& row:source->tags){if(row.id==id){return row.uniqueHashes.size();}}return 998;}
void announce(const QString& path,const std::string& url){auto state=core::Document::parse(test::read(path).toStdString());test::require(state&&bool(state->replaceValue("announce",std::to_string(url.size())+':'+url)),"Generated original announce mutation");test::write(path,QByteArray::fromStdString(state->serialize()));}
}
class TrackerTagSyncTests final:public QObject {
 Q_OBJECT
private slots:
 void widgetUsesSavedFilterAndNormalPendingProtocol(){
  auto source=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.trackersl12:example.testeeeee");QVERIFY(source);auto packet=std::make_shared<ui::ManualTagCommandPacket>();packet->session=73;packet->request=1;packet->generation=2;packet->readState=ui::ManualTagReadState::Ready;packet->status=ui::ManualTagCommandStatus::Loaded;packet->writable=true;packet->tags=std::make_shared<const core::SavedDownloadTags>(*source);
  ui::ManualTagManagerDialog dialog(73);QSignalSpy commands(&dialog,&ui::ManualTagManagerDialog::commandRequested);dialog.setPresentation(packet);auto* sync=dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-trackers-sync"));QVERIFY(sync&&sync->isEnabled());const auto capture=qEnvironmentVariable("BIGLYBTC_TRACKER_TAG_SYNC_CAPTURE");if(!capture.isEmpty()){dialog.show();QCoreApplication::processEvents();QVERIFY(dialog.grab().save(capture,"PNG"));}
  sync->click();QCOMPARE(commands.count(),1);auto request=qvariant_cast<ui::ManualTagCommand>(commands.last()[0]);QCOMPARE(request.kind,ui::ManualTagCommandKind::SyncTrackers);QCOMPARE(request.tagId,1);QVERIFY(!request.trackerNames);request.request=2;dialog.setPending(request);QVERIFY(!sync->isEnabled());packet->request=2;packet->kind=ui::ManualTagCommandKind::SyncTrackers;packet->status=ui::ManualTagCommandStatus::Unchanged;dialog.setResult(packet);QVERIFY(sync->isEnabled());
  auto empty=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generatedeee");QVERIFY(empty);packet->request=3;packet->generation=3;packet->tags=std::make_shared<const core::SavedDownloadTags>(*empty);dialog.setPresentation(packet);QVERIFY(!sync->isEnabled());
  auto malformed=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.trackersi1eeeee");QVERIFY(malformed);packet->request=4;packet->generation=4;packet->tags=std::make_shared<const core::SavedDownloadTags>(*malformed);dialog.setPresentation(packet);QVERIFY(!sync->isEnabled());dialog.invalidateSession();QVERIFY(!sync->isEnabled());
 }
 void offlineSessionCannotProduceTrackerSyncEffects(){
  test::OwnedEngineFixture f(false);libtorrent::entry tags(libtorrent::entry::dictionary_type{});tags["3"]["1"]["n"]="Generated";tags["3"]["1"]["c"]["pp.trackers"]=libtorrent::entry::list_type{libtorrent::entry("example.test")};test::write(f.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));const auto before=test::read(f.profile+"/tag.config");
  app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::manualTagCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);ui::ManualTagCommand command;command.session=sessions.last()[0].toULongLong();command.request=1;QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==1);QVERIFY(latest(done));command.kind=ui::ManualTagCommandKind::SyncTrackers;command.tagId=1;command.request=2;command.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(latest(done)->failure.contains("normal session"));QCOMPARE(test::read(f.profile+"/tag.config"),before);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
 }
 void explicitSyncUsesCurrentSourceAndEmptyKeepsMembers(){
  test::OwnedEngineFixture fixture(false);const auto inventory=fixture.profile+"/downloads.config";auto source=test::read(inventory);QCOMPARE(source.count("5:statei75e"),1);source.replace("5:statei75e","5:statei70e");test::write(inventory,source);announce(fixture.stateFile,"http://a.example.test/announce");
  libtorrent::entry tags(libtorrent::entry::dictionary_type{});tags["3"]["1"]["n"]="New only control";tags["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("size>0"),libtorrent::entry("am=3;")};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));
  libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;settings["TCP.Listen.Port"]=6881;settings["UDP.Listen.Port"]=6881;settings["IPV6 Enable Support"]=0;
  for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}settings["Tag Auto Full Reapply Period Secs"]=0;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
  app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::manualTagCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(fixture.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
  ui::ManualTagCommand request;request.session=sessions.last()[0].toULongLong();request.request=1;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==1);QVERIFY(latest(done));
  request.kind=ui::ManualTagCommandKind::Create;request.request=2;request.generation=latest(done)->generation;request.name="Fresh tracker tag";QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QVERIFY(latest(done)->createdId);const auto id=*latest(done)->createdId;
  request.kind=ui::ManualTagCommandKind::SetTrackers;request.request=3;request.generation=latest(done)->generation;request.tagId=id;request.trackerNames=QStringList{"example.test"};QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==3);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);
  QTRY_COMPARE_WITH_TIMEOUT(count(fixture.profile+"/tag.config",id),std::size_t{1},10000);QCOMPARE(count(fixture.profile+"/tag.config",1),std::size_t{0});
  request.kind=ui::ManualTagCommandKind::Reload;request.request=4;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==4);const auto before=test::read(fixture.profile+"/tag.config");announce(fixture.stateFile,"http://other.invalid/announce");
  request.kind=ui::ManualTagCommandKind::SetTrackers;request.request=5;request.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==5);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Unchanged);
  QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}QVERIFY(idle);QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=2,4000);QCOMPARE(test::read(fixture.profile+"/tag.config"),before);
  const auto filterBytes=[&]{auto source=core::Document::parse(test::read(fixture.profile+"/tag.config").toStdString());return core::encodeValue(*source->find("3")->find(std::to_string(id))->find("c")).value();};const auto savedFilters=filterBytes();const auto generation=latest(done)->generation;
  request.kind=ui::ManualTagCommandKind::SyncTrackers;request.request=6;request.generation=generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==6);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Unchanged);QCOMPARE(latest(done)->generation,generation);QVERIFY(latest(done)->constraintStatus.contains("queued"));
  QTRY_COMPARE_WITH_TIMEOUT(count(fixture.profile+"/tag.config",id),std::size_t{0},10000);QCOMPARE(filterBytes(),savedFilters);QCOMPARE(count(fixture.profile+"/tag.config",1),std::size_t{0});
  request.kind=ui::ManualTagCommandKind::Reload;request.request=7;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==7);
  request.kind=ui::ManualTagCommandKind::SetTrackers;request.request=8;request.generation=latest(done)->generation;request.trackerNames.reset();QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==8);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);const auto clearedAt=turns.count();QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=clearedAt+2,4000);
  request.kind=ui::ManualTagCommandKind::Reload;request.request=9;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==9);
  request.kind=ui::ManualTagCommandKind::AddMember;request.request=10;request.generation=latest(done)->generation;request.member={QString::fromStdString(fixture.hash->hex()),latest(done)->libraryRevision};QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==10);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(count(fixture.profile+"/tag.config",id),std::size_t{1});
  const auto manualMember=test::read(fixture.profile+"/tag.config");request.kind=ui::ManualTagCommandKind::SyncTrackers;request.request=11;request.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==11);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Unchanged);QVERIFY(latest(done)->constraintStatus.contains("No saved"));const auto syncedAt=turns.count();QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=syncedAt+2,4000);QCOMPARE(test::read(fixture.profile+"/tag.config"),manualMember);
  request.request=12;request.generation=1;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==12);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(fixture.profile+"/tag.config"),manualMember);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),char(0)));}
 }
};
QTEST_MAIN(TrackerTagSyncTests)
#include "tracker_tag_sync_tests.moc"
