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
class TrackerTagEditorTests final:public QObject {
 Q_OBJECT
private slots:
 void widgetUsesOriginalSeparatorsAndRetainsMalformedSource(){
  auto parsed=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.trackersl12:example.testeeeee");QVERIFY(parsed);
  auto packet=std::make_shared<ui::ManualTagCommandPacket>();packet->session=73;packet->request=1;packet->generation=2;packet->readState=ui::ManualTagReadState::Ready;packet->status=ui::ManualTagCommandStatus::Loaded;packet->writable=true;packet->tags=std::make_shared<const core::SavedDownloadTags>(*parsed);
  ui::ManualTagManagerDialog dialog(73);QSignalSpy commands(&dialog,&ui::ManualTagManagerDialog::commandRequested);dialog.setPresentation(packet);
  auto* text=dialog.findChild<QLineEdit*>(QStringLiteral("manual-tag-trackers"));auto* save=dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-trackers-save"));QVERIFY(text&&save&&text->isEnabled()&&save->isEnabled());QCOMPARE(text->text(),QStringLiteral("example.test"));
  const auto capture=qEnvironmentVariable("BIGLYBTC_TRACKER_TAG_EDITOR_CAPTURE");if(!capture.isEmpty()){dialog.show();QCoreApplication::processEvents();QVERIFY(dialog.grab().save(capture,"PNG"));}
  text->setText(QStringLiteral(" A.test ; b.test ,,\tc.test\t "));save->click();QCOMPARE(commands.count(),1);auto request=qvariant_cast<ui::ManualTagCommand>(commands.last()[0]);QCOMPARE(request.kind,ui::ManualTagCommandKind::SetTrackers);QCOMPARE(request.tagId,1);QVERIFY(request.trackerNames);QCOMPARE(*request.trackerNames,QStringList({"A.test","b.test","c.test"}));
  text->setText(QString::fromUtf8("caf\xc3\xa9.test"));QVERIFY(!save->isEnabled());text->clear();QVERIFY(save->isEnabled());save->click();QCOMPARE(commands.count(),2);QVERIFY(!qvariant_cast<ui::ManualTagCommand>(commands.last()[0]).trackerNames);
  auto malformed=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.trackersi1eeeee");QVERIFY(malformed);packet->request=3;packet->generation=3;packet->tags=std::make_shared<const core::SavedDownloadTags>(*malformed);dialog.setPresentation(packet);QVERIFY(!text->isEnabled()&&!save->isEnabled());
  auto malformedConfig=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:ci1eeee");QVERIFY(malformedConfig);packet->request=4;packet->generation=4;packet->tags=std::make_shared<const core::SavedDownloadTags>(*malformedConfig);dialog.setPresentation(packet);QVERIFY(!text->isEnabled()&&!save->isEnabled());
 }
 void changedDurableSetterHandlesNewTagNoopAndClearWithoutBirth(){
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
  request.trackerNames.reset();request.request=6;request.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==6);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);QTRY_COMPARE_WITH_TIMEOUT(count(fixture.profile+"/tag.config",id),std::size_t{0},10000);QCOMPARE(count(fixture.profile+"/tag.config",1),std::size_t{0});
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),char(0)));}
 }
};
QTEST_MAIN(TrackerTagEditorTests)
#include "tracker_tag_editor_tests.moc"
