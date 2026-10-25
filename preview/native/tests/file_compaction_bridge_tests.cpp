#include "session_bridge.hpp"
#include "owned_engine_fixture.hpp"
#include "original_file_io.hpp"
#include "persistent_session.hpp"
#include <array>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

using namespace biglybtc;
namespace {
std::string bytes(std::string_view value) { return std::to_string(value.size())+':'+std::string(value); }
std::string number(std::size_t value) { return 'i'+std::to_string(value)+'e'; }
core::Document parse(const QByteArray& bytes) {
  auto parsed=core::Document::parse(bytes.toStdString(),storage::compatibleConfigLimits());
  test::require(parsed.has_value(),"Parse generated compaction fixture"); return std::move(*parsed);
}
void put(core::Document& document,std::string_view key,std::string value) {
  test::require(document.replaceValue(key,std::move(value)).has_value(),"Edit generated compaction fixture");
}
std::string encoded(const core::Node& node) {
  auto result=core::encodeValue(node,storage::compatibleConfigLimits());
  test::require(result.has_value(),"Encode generated compaction fixture"); return std::move(*result);
}
ui::Presentation latest(const QSignalSpy& spy) { return spy.empty()?ui::Presentation{}:qvariant_cast<ui::Presentation>(spy.last()[0]); }
ui::Target target(const QSignalSpy& rows,const core::TorrentHash& hash) {
  const auto value=latest(rows);test::require(value!=nullptr,"Actual library packet required");
  return {QString::fromStdString(hash.hex()),value->revision};
}
ui::Details detail(const QSignalSpy& spy) { return spy.empty()?ui::Details{}:qvariant_cast<ui::Details>(spy.last()[0]); }
struct Fixture {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-file-compaction-host-XXXXXX")};
  QString profile=root.path()+QStringLiteral("/profile"),payload=root.path()+QStringLiteral("/payload/bundle"),state;
  core::TorrentHash hash;
  std::array<QByteArray,3> data{QByteArray(3072,'l'),QByteArray(40000,'m'),QByteArray(6000,'r')};
  Fixture() {
    test::require(root.isValid(),"Owned compaction root");
    std::string files="l";QByteArray all;
    for(std::size_t j=0;j<data.size();++j) {
      files+="d6:length"+number(static_cast<std::size_t>(data[j].size()))+"4:pathl"+bytes(std::to_string(j)+".bin")+"ee";
      all+=data[j];test::write(payload+'/'+QString::number(static_cast<qulonglong>(j))+QStringLiteral(".bin"),data[j]);
    }
    auto info=parse("de");put(info,"files",files+'e');put(info,"name",bytes("bundle"));put(info,"piece length","i16384e");
    std::string pieces;
    for(qsizetype at=0;at<all.size();at+=16384) { pieces+=QCryptographicHash::hash(all.mid(at,16384),QCryptographicHash::Sha1).toStdString(); }
    put(info,"pieces",bytes(pieces));
    const auto raw=QCryptographicHash::hash(QByteArray::fromStdString(info.serialize()),QCryptographicHash::Sha1);
    hash=*core::TorrentHash::parse(raw.toStdString());state=profile+"/active/"+QString::fromStdString(hash.hex())+".dat";
    auto torrent=parse("de");put(torrent,"info",info.serialize());
    auto attributes=parse("de");put(attributes,"opaque.state",bytes("retained"));put(attributes,"storetypes","l1:L1:L1:Le");
    put(torrent,"attributes",attributes.serialize());
    test::write(state,QByteArray::fromStdString(torrent.serialize()));
    auto row=parse("de");put(row,"torrent_hash",bytes(raw.toStdString()));put(row,"torrent",bytes((root.path()+"/archive.torrent").toStdString()));
    put(row,"save_dir",bytes((root.path()+"/payload").toStdString()));put(row,"save_file",bytes("bundle"));put(row,"persistent","i1e");
    put(row,"state","i70e");put(row,"position","i7e");put(row,"forceStart","i1e");put(row,"file_priorities","li-1ei-1ei-1ee");
    put(row,"opaque.row","d6:retain3:rawe");put(row,"downloaded","i777e");put(row,"uploaded","i999e");
    auto inventory=parse("de");put(inventory,"downloads","l"+row.serialize()+'e');
    put(inventory,"pause_data","ld5:forcei1e4:hash"+bytes(raw.toStdString())+"ee");
    test::write(profile+"/downloads.config",QByteArray::fromStdString(inventory.serialize()));
    test::write(root.path()+"/archive.torrent",QByteArray::fromStdString(torrent.serialize()));
    test::write(profile+"/tables.config","de");test::write(profile+"/BiglyBT.statistics","d3:alld10:downloadedi999eee");
  }
  std::filesystem::path native() const { return profile.toStdString(); }
  QString file() const { return payload+"/1.bin"; }
};
void stopSaved(test::OwnedEngineFixture& f) {
  auto config=parse(test::read(f.profile+"/downloads.config"));
  auto row=core::Document::parse(encoded(config.find("downloads")->children[0]),storage::compatibleConfigLimits());
  test::require(row.has_value(),"Generated row");put(*row,"state","i70e");
  put(config,"downloads","l"+row->serialize()+'e');test::write(f.profile+"/downloads.config",QByteArray::fromStdString(config.serialize()));
}
}
class FileCompactionBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    auto owner=engine::PersistentSession::create({9081,9082,9083});
    QVERIFY2(owner.has_value(),"This actual-owner cohort requires the approved source runtime; no skip");
    QVERIFY((*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds(5)).sessionJoined);
  }
  void offlineWorkerRejectsStaleThenPacksActualBoundaryBytesAndPreservesIntent() {
    Fixture f;app::SessionBridge bridge;
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),details(&bridge,&app::SessionBridge::detailsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows),10000);
    const auto old=target(rows,f.hash);QVERIFY(bridge.details(old));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&detail(details).files,10000);
    QVERIFY(detail(details).files->canCompact);QVERIFY(detail(details).files->items[1].canCompact);
    const auto inventoryBefore=test::read(f.profile+"/downloads.config"),payloadBefore=test::read(f.file());
    auto stale=old;++stale.generation;QVERIFY(bridge.compactFile(stale,1));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    QCOMPARE(test::read(f.profile+"/downloads.config"),inventoryBefore);QCOMPARE(test::read(f.file()),payloadBefore);
    // Rejection republishes a new actual library generation. Use its current
    // Target for the separate successful command, rather than retrying stale.
    const auto currentForCompaction=target(rows,f.hash);
    QVERIFY(bridge.compactFile(currentForCompaction,1));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows)->revision!=currentForCompaction.generation,10000);
    QCOMPARE(test::read(f.file()),f.data[1].first(13312)+f.data[1].mid(29696));
    QCOMPARE(test::read(f.payload+"/0.bin"),f.data[0]);QCOMPARE(test::read(f.payload+"/2.bin"),f.data[2]);
    const auto state=parse(test::read(f.state));QCOMPARE(state.find("attributes")->find("storetypes")->children[1].bytes,std::string_view("C"));
    QCOMPARE(state.find("attributes")->find("opaque.state")->bytes,std::string_view("retained"));
    const auto before=parse(inventoryBefore),after=parse(test::read(f.profile+"/downloads.config"));
    const auto& prior=before.find("downloads")->children[0];const auto& next=after.find("downloads")->children[0];
    for(const auto* key:{"forceStart","state","position","downloaded","uploaded","opaque.row"}) { QCOMPARE(encoded(*next.find(key)),encoded(*prior.find(key))); }
    QCOMPARE(encoded(*after.find("pause_data")),encoded(*before.find("pause_data")));
    QVERIFY(!QFileInfo::exists(f.profile+"/.BiglyBTC.linear-to-compact"));
    const auto current=target(rows,f.hash);const auto previous=details.count();QVERIFY(bridge.details(current));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&details.count()>previous,10000);
    QVERIFY(!detail(details).files->items[1].canCompact);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
    auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
  }
  void actualRetainedOwnerRejectsCompactionUntilAllHandlesHaveRetired() {
    test::OwnedEngineFixture f(true,true);stopSaved(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),details(&bridge,&app::SessionBridge::detailsReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed),native(&bridge,&app::SessionBridge::nativeSessionReady);
    QVERIFY(bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows),10000);
    QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    QVERIFY(bridge.details(target(rows,*f.hash)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&detail(details).files,10000);
    QVERIFY(!detail(details).files->canCompact);
    const auto messageCount=messages.count();QVERIFY(bridge.compactFile(target(rows,*f.hash),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&messages.count()>messageCount,10000);
    QVERIFY(!QFileInfo::exists(f.profile+"/.BiglyBTC.linear-to-compact"));
    const auto unchanged=parse(test::read(f.stateFile));QCOMPARE(unchanged.find("attributes")->find("storetypes")->children[0].bytes,std::string_view("L"));
    QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Stop));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
      const auto snapshot=latest(rows);if(!snapshot){return false;}
      const auto found=std::ranges::find(snapshot->items,QString::fromStdString(f.hash->hex()),&ui::LibraryItem::hash);
      return found!=snapshot->items.end()&&found->state==QStringLiteral("Stopped");
    }(),15000);
    QTRY_VERIFY_WITH_TIMEOUT(!native.empty() && qvariant_cast<ui::NativeSessionPresentation>(native.last()[0])
        && qvariant_cast<ui::NativeSessionPresentation>(native.last()[0])->state==ui::NativeSessionState::Idle,10000);
    // The command itself additionally checks sole-pump success, idle state and
    // actual flush/removal/weak-expiration through forgetManaged for ALL rows.
    QVERIFY(bridge.compactFile(target(rows,*f.hash),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&QFileInfo(f.payload+'/'+f.files[0].relative).size()==0,10000);
    const auto compacted=parse(test::read(f.stateFile));QCOMPARE(compacted.find("attributes")->find("storetypes")->children[0].bytes,std::string_view("C"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
    auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
  }
};
QTEST_MAIN(FileCompactionBridgeTests)
#include "file_compaction_bridge_tests.moc"
