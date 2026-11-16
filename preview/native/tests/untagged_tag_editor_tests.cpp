// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "manual_tag_editor.hpp"
#include "owned_engine_fixture.hpp"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <fstream>
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
class UntaggedTagEditorTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalBooleanReaderProjection(){
  std::ifstream input(UNTAGGED_TAG_PROPERTY_FIXTURE,std::ios::binary);const std::string encoded{std::istreambuf_iterator<char>(input),{}};auto original=core::Document::parse(encoded);QVERIFY(original);unsigned cases=0;
  for(const auto& row:original->find("rows")->children){const auto before="d1:3d1:1d1:n9:Generated1:cd"+std::string(row.find("before_present")->integer?"11:pp.untaggedi"+std::to_string(row.find("before")->integer)+"e":"")+"eeee";auto tags=core::readSavedDownloadTags(before);QVERIFY(tags);auto packet=std::make_shared<ui::ManualTagCommandPacket>();packet->session=1;packet->generation=1;packet->request=1;packet->readState=ui::ManualTagReadState::Ready;packet->status=ui::ManualTagCommandStatus::Loaded;packet->writable=true;packet->tags=std::make_shared<const core::SavedDownloadTags>(*tags);auto projected=ui::manualTagEditorValues(packet);QVERIFY(projected&&projected->rows.size()==1&&projected->rows.front().untaggedEditable);const auto actual=projected->rows.front().untagged;QCOMPARE(actual.has_value(),row.find("read")->integer!=-1);if(actual){QCOMPARE(*actual,row.find("read")->integer==1);}++cases;}QCOMPARE(cases,18U);
 }
 void widgetProjectsBooleanDefaultAndRetainsMalformed(){
  auto parsed=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.untaggedi1eeeee");QVERIFY(parsed);
  auto packet=std::make_shared<ui::ManualTagCommandPacket>();packet->session=73;packet->request=1;packet->generation=2;packet->readState=ui::ManualTagReadState::Ready;packet->status=ui::ManualTagCommandStatus::Loaded;packet->writable=true;packet->tags=std::make_shared<const core::SavedDownloadTags>(*parsed);
  ui::ManualTagManagerDialog dialog(73);QSignalSpy commands(&dialog,&ui::ManualTagManagerDialog::commandRequested);dialog.setPresentation(packet);
  auto* flag=dialog.findChild<QCheckBox*>(QStringLiteral("manual-tag-untagged"));auto* save=dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-untagged-save"));auto* reset=dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-untagged-default"));QVERIFY(flag&&save&&reset&&flag->isEnabled()&&flag->isChecked()&&reset->isEnabled());
  const auto capture=qEnvironmentVariable("BIGLYBTC_UNTAGGED_TAG_EDITOR_CAPTURE");if(!capture.isEmpty()){dialog.show();QCoreApplication::processEvents();QVERIFY(dialog.grab().save(capture,"PNG"));}
  flag->setChecked(false);save->click();QCOMPARE(commands.count(),1);auto request=qvariant_cast<ui::ManualTagCommand>(commands.last()[0]);QCOMPARE(request.kind,ui::ManualTagCommandKind::SetUntagged);QCOMPARE(request.tagId,1);QVERIFY(request.untagged.has_value()&&!*request.untagged);
  reset->click();QCOMPARE(commands.count(),2);QVERIFY(!qvariant_cast<ui::ManualTagCommand>(commands.last()[0]).untagged);
  auto absent=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generatedeee");QVERIFY(absent);packet->request=2;packet->generation=3;packet->tags=std::make_shared<const core::SavedDownloadTags>(*absent);dialog.setPresentation(packet);QVERIFY(flag->isEnabled()&&!flag->isChecked()&&!reset->isEnabled());
  auto malformed=core::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.untagged1:xeeee");QVERIFY(malformed);packet->request=3;packet->generation=4;packet->tags=std::make_shared<const core::SavedDownloadTags>(*malformed);dialog.setPresentation(packet);QVERIFY(!flag->isEnabled()&&!save->isEnabled()&&!reset->isEnabled());dialog.invalidateSession();QVERIFY(!save->isEnabled());
 }
 void normalSavedBooleanEnableDisableNoopDefaultAndStale(){
  test::OwnedEngineFixture fixture(false);const auto inventory=fixture.profile+"/downloads.config";auto source=test::read(inventory);QCOMPARE(source.count("5:statei75e"),1);source.replace("5:statei75e","5:statei70e");test::write(inventory,source);announce(fixture.stateFile,"http://a.example.test/announce");
  libtorrent::entry tags(libtorrent::entry::dictionary_type{});tags["3"]["1"]["n"]="New only control";tags["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("size>0"),libtorrent::entry("am=3;")};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));
  libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;settings["TCP.Listen.Port"]=6881;settings["UDP.Listen.Port"]=6881;settings["IPV6 Enable Support"]=0;
  for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}settings["Tag Auto Full Reapply Period Secs"]=0;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
  app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::manualTagCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(fixture.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
  ui::ManualTagCommand request;request.session=sessions.last()[0].toULongLong();request.request=1;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==1);QVERIFY(latest(done));
  request.kind=ui::ManualTagCommandKind::Create;request.request=2;request.generation=latest(done)->generation;request.name="Fresh Untagged tag";QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QVERIFY(latest(done)->createdId);const auto id=*latest(done)->createdId;
  request.kind=ui::ManualTagCommandKind::SetUntagged;request.request=3;request.generation=latest(done)->generation;request.tagId=id;request.untagged=true;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==3);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);
  QTRY_COMPARE_WITH_TIMEOUT(count(fixture.profile+"/tag.config",id),std::size_t{1},10000);QCOMPARE(count(fixture.profile+"/tag.config",1),std::size_t{0});
  request.kind=ui::ManualTagCommandKind::Reload;request.request=4;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==4);const auto before=test::read(fixture.profile+"/tag.config");announce(fixture.stateFile,"http://other.invalid/announce");
  request.kind=ui::ManualTagCommandKind::SetUntagged;request.request=5;request.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==5);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Unchanged);
  QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}QVERIFY(idle);QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=2,4000);QCOMPARE(test::read(fixture.profile+"/tag.config"),before);
  request.untagged=false;request.request=6;request.generation=latest(done)->generation;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==6);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);QTRY_COMPARE_WITH_TIMEOUT(count(fixture.profile+"/tag.config",id),std::size_t{0},10000);QCOMPARE(count(fixture.profile+"/tag.config",1),std::size_t{0});
  request.kind=ui::ManualTagCommandKind::Reload;request.request=7;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==7);
  request.kind=ui::ManualTagCommandKind::SetUntagged;request.request=8;request.generation=latest(done)->generation;request.untagged.reset();QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==8);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);QVERIFY(!latest(done)->tags->source->find("3")->find(std::to_string(id))->find("c")->find("pp.untagged"));
  const auto beforeRejected=test::read(fixture.profile+"/tag.config");request.request=9;request.generation=1;request.untagged=true;QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&done.count()==9);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(fixture.profile+"/tag.config"),beforeRejected);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),char(0)));}
 }
};
QTEST_MAIN(UntaggedTagEditorTests)
#include "untagged_tag_editor_tests.moc"
