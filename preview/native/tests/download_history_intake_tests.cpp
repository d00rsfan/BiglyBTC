// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "torrent_intake_dialog.hpp"
#include "durable_intake_receipt.hpp"
#include "download_history_store.hpp"
#include "magnet_intake.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <future>
#include <iterator>
#include <stdexcept>
#include <type_traits>
using namespace biglybtc;
namespace {
std::atomic_bool historyReadWatch=false,historyReadEntered=false,historyWriterTrying=false,historyReadAbort=false;
std::atomic<std::uintmax_t> historyReadDevice=0,historyReadInode=0;
std::mutex historyReadMutex;std::condition_variable historyReadWake;
thread_local bool historyReadWorker=false;
void check(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,storage::Error> v){if(!v){throw std::runtime_error(v.error().message);}return std::move(*v);}
void take(std::expected<void,storage::Error> v){if(!v){throw std::runtime_error(v.error().message);}}
std::string encode(const libtorrent::entry& v){std::string out;libtorrent::bencode(std::back_inserter(out),v);return out;}
void write(const QString& p,std::string_view v){QFile f(p);check(f.open(QIODevice::WriteOnly)&&f.write(v.data(),static_cast<qint64>(v.size()))==static_cast<qint64>(v.size())&&f.flush(),"Fixture write");}
std::string read(const QString& p){QFile f(p);check(f.open(QIODevice::ReadOnly),"Fixture read");return f.readAll().toStdString();}
struct Area {
  QTemporaryDir area{QStringLiteral("/tmp/biglybtc-history-intake-XXXXXX")};QString profile,metadata,payload;core::TorrentHash hash;libtorrent::entry settings;
  explicit Area(bool enabled=true,std::int64_t flags=0){
    check(area.isValid(),"Fresh fixture");profile=area.path()+"/profile";metadata=area.path()+"/input.torrent";payload=area.path()+"/payload";check(QDir().mkpath(profile),"Profile directory");
    libtorrent::entry d(libtorrent::entry::dictionary_type{});auto& info=d["info"];info["name"]="fresh.bin";info["length"]=1;info["piece length"]=16384;info["pieces"]=QCryptographicHash::hash(QByteArray("x"),QCryptographicHash::Sha1).toStdString();
    const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(encode(info)),QCryptographicHash::Sha1);auto key=core::TorrentHash::parse(std::string_view(digest.constData(),static_cast<std::size_t>(digest.size())));check(bool(key),"Raw generated hash");hash=*key;
    d["attributes"]["stats.download.added.time"]=1900;d["attributes"]["parameters"]["stats.download.added.time"]=123;d["attributes"]["complt"]=-1;d["attributes"]["parameters"]["stats.download.completed.time"]=9876;d["attributes"]["flags"]=flags;d["future-root"]="keep";write(metadata,encode(d));
    write(profile+"/downloads.config","d9:downloadslee");write(profile+"/tables.config","de");settings=libtorrent::entry(libtorrent::entry::dictionary_type{});settings["Download History Enabled"]=enabled?1:0;settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;settings["IPV6 Enable Support"]=0;
    for(const auto* k:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[k]=0;}
    write(profile+"/biglybt.config",encode(settings));write(area.path()+"/sentinel","payload unchanged");
  }
  std::filesystem::path path()const{return profile.toStdString();}
  QString state()const{return profile+"/active/"+QString::fromStdString(hash.hex())+".dat";}
};
std::vector<core::SavedDownloadHistoryRecord> rows(const Area& a){if(!QFile::exists(a.profile+"/dlhistorya.config")){return {};}auto v=core::readSavedDownloadHistory(read(a.profile+"/dlhistorya.config"));check(bool(v),"Actual history parse");return v->records;}
storage::IntakeHistoryOutcome imported(Area& a,storage::ProfileLease& lease){storage::IntakeHistoryOutcome out;auto v=storage::importTorrent(lease,a.metadata.toStdString(),a.payload.toStdString(),true,nullptr,false,storage::TorrentIntakeNetwork::Public,&out);check(v&&*v==a.hash&&out.receipt&&!out.warning,"Fresh durable receipt absent");return out;}
}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void* out,size_t count){
  if(historyReadWorker&&historyReadWatch){struct stat value{};
    if(::fstat(fd,&value)==0&&static_cast<std::uintmax_t>(value.st_dev)==historyReadDevice&&static_cast<std::uintmax_t>(value.st_ino)==historyReadInode&&!historyReadEntered.exchange(true)){
      historyReadWake.notify_all();std::unique_lock lock(historyReadMutex);
      (void)historyReadWake.wait_for(lock,std::chrono::seconds(5),[]{return historyWriterTrying.load()||historyReadAbort.load();});
    }
  }
  return __real_read(fd,out,count);
}
class DownloadHistoryIntakeTests final:public QObject {
  Q_OBJECT
private slots:
  void recoveredAlreadyImportedMagnetCannotMintHistory(){
    Area a;auto work=std::async(std::launch::async,[&]{
      auto lease=take(storage::ProfileLease::acquire(a.path()));auto policy=engine::readNetworkPolicy(nullptr,{engine::NetworkMode::Normal});check(bool(policy),"Actual normal policy");
      auto parsed=take(engine::parseMagnetIntent("magnet:?xt=urn:btih:"+a.hash.hex(),a.payload.toStdString(),true,*policy));
      auto pending=take(engine::queuePendingMagnet(lease,parsed,*policy));
      // Real durable ordinary import, deliberately WITHOUT a history receipt.
      (void)take(storage::importTorrent(lease,a.metadata.toStdString(),a.payload.toStdString(),true));
      const auto inventory=read(a.profile+"/downloads.config");int calls=0;bool newly=true;std::optional<storage::Error> warning;
      auto completed=engine::completePendingMagnet(lease,pending,a.metadata.toStdString(),*policy,&newly,
          [&](storage::IntakeHistoryOutcome)->std::optional<storage::Error>{++calls;return {};},&warning);
      check(completed&&*completed==a.hash&&!newly&&calls==0&&!warning,"Already-present recovery issued fresh history");
      check(read(a.profile+"/downloads.config")==inventory&&!QFile::exists(a.profile+"/dlhistorya.config"),"Recovery rewrote inventory/backfilled history");
      auto remaining=take(engine::readPendingMagnets(lease,*policy));check(remaining.empty(),"Actual recovery pending cleanup");
      check(!engine::completePendingMagnet(lease,pending,a.metadata.toStdString(),*policy,&newly,
          [&](storage::IntakeHistoryOutcome)->std::optional<storage::Error>{++calls;return {};},&warning)&&calls==0,"Duplicate completion manufactured history");
    });work.get();
  }
  void genuineReceiptIsSingleUseAndReopenPreservesOriginalInputs(){
    static_assert(!std::is_default_constructible_v<storage::DurableIntakeReceipt>);static_assert(!std::is_copy_constructible_v<storage::DurableIntakeReceipt>);
    Area a;auto worker=std::async(std::launch::async,[&]{
      auto lease=take(storage::ProfileLease::acquire(a.path()));auto out=imported(a,lease);auto store=take(storage::DownloadHistoryStore::open(lease));auto snap=take(store->snapshot());auto saved=take(store->appendIntake(snap,std::move(*out.receipt)));check(saved.changed,"Real append refused");
      const auto& records=saved.snapshot.history(storage::DownloadHistorySide::Active)->records;check(records.size()==1,"History row missing");const auto& row=records[0];const auto raw=std::string(reinterpret_cast<const char*>(a.hash.bytes().data()),a.hash.bytes().size());
      check(row.uid==core::originalDownloadHistoryUid(raw,1900)&&row.addTime>123&&row.completeTime==0&&row.removeTime==0&&row.size==1&&row.hash==raw&&row.name=="fresh.bin"&&row.saveLocation==(a.payload+"/fresh.bin").toStdString(),"Original UID/parameter/fullhash/name/path/size/completion mismatch");
      const auto bytes=read(a.profile+"/dlhistorya.config");check(!store->appendIntake(saved.snapshot,std::move(*out.receipt)),"Consumed receipt replay");storage::IntakeHistoryOutcome duplicate;check(!storage::importTorrent(lease,a.metadata.toStdString(),a.payload.toStdString(),true,nullptr,false,storage::TorrentIntakeNetwork::Public,&duplicate)&&!duplicate.receipt,"Existing import issued receipt");
      check(!store->apply(saved.snapshot,storage::DownloadHistorySide::Active,core::DownloadHistoryAppend{}),"Generic append minted ownership");take(store->close());auto reopened=take(storage::DownloadHistoryStore::open(lease));check(take(reopened->snapshot()).history(storage::DownloadHistorySide::Active)->records.size()==1&&read(a.profile+"/dlhistorya.config")==bytes,"Reopen duplicated/rewrote event");check(!QFile::exists(a.payload+"/fresh.bin")&&read(a.area.path()+"/sentinel")=="payload unchanged","History touched payload");
    });worker.get();
  }
  void staleForeignCancelledClosedAndChangedSourcesRefuse_data(){QTest::addColumn<int>("which");for(int n=0;n<7;++n){QTest::newRow(qPrintable(QString::number(n)))<<n;}}
  void staleForeignCancelledClosedAndChangedSourcesRefuse(){
    QFETCH(int,which);Area a;Area foreign;auto worker=std::async(std::launch::async,[&]{auto lease=take(storage::ProfileLease::acquire(a.path()));auto out=imported(a,lease);auto store=take(storage::DownloadHistoryStore::open(lease));auto snap=take(store->snapshot());std::stop_source cancel;
      if(which==0){cancel.request_stop();}if(which==1){auto changed=take(lease.sourceAccess(true));}if(which==2){write(a.state(),read(a.state())+"x");}if(which==3){a.settings["Download History Enabled"]=0;write(a.profile+"/biglybt.config",encode(a.settings));}if(which==4){take(store->close());}if(which==5){write(a.profile+"/dlhistorya.config.saving","de");}
      std::optional<storage::ProfileLease> other;std::unique_ptr<storage::DownloadHistoryStore> foreignStore;if(which==6){other.emplace(take(storage::ProfileLease::acquire(foreign.path())));foreignStore=take(storage::DownloadHistoryStore::open(*other));}
      auto result=which==6?foreignStore->appendIntake(take(foreignStore->snapshot()),std::move(*out.receipt)):store->appendIntake(snap,std::move(*out.receipt),cancel.get_token());check(!result&&!QFile::exists(a.profile+"/dlhistorya.config")&&!QFile::exists(foreign.profile+"/dlhistorya.config")&&QFile::exists(a.state()),"Rejected event created history or rolled back intake");
    });worker.get();
  }
  void disabledExcludedPreviewAndStartupDoNotCreate_data(){QTest::addColumn<bool>("enabled");QTest::addColumn<qint64>("flags");QTest::newRow("disabled")<<false<<qint64(0);QTest::newRow("low-noise")<<true<<qint64(0x10);QTest::newRow("metadata")<<true<<qint64(0x200);}
  void disabledExcludedPreviewAndStartupDoNotCreate(){
    QFETCH(bool,enabled);QFETCH(qint64,flags);Area a(enabled,flags);auto worker=std::async(std::launch::async,[&]{auto lease=take(storage::ProfileLease::acquire(a.path()));(void)take(storage::inspectTorrentIntake(a.metadata.toStdString()));auto store=take(storage::DownloadHistoryStore::open(lease));check(!QFile::exists(a.profile+"/dlhistorya.config"),"Preview created history");auto out=imported(a,lease);auto result=take(store->appendIntake(take(store->reload()),std::move(*out.receipt)));check(!result.changed&&!QFile::exists(a.profile+"/dlhistorya.config"),"Disabled/excluded event created history");take(store->close());auto reopened=take(storage::DownloadHistoryStore::open(lease));check(take(reopened->snapshot()).missing(storage::DownloadHistorySide::Active),"Reopen backfilled event");});worker.get();
  }
  void realSourceWriterCannotEnterBetweenIntakeReadAndOwnWrite(){
    Area a;core::DownloadHistoryNewRecord old;old.uid=99;old.name="prior";old.saveLocation="/old";
    auto encoded=core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{old});QVERIFY(encoded);write(a.profile+"/dlhistorya.config",encoded->value.source->originalBytes());
    auto worker=std::async(std::launch::async,[&]{
      auto lease=take(storage::ProfileLease::acquire(a.path()));auto event=imported(a,lease);auto store=take(storage::DownloadHistoryStore::open(lease));auto snapshot=take(store->snapshot());struct stat selected{};
      check(::stat((a.profile+"/dlhistorya.config").toUtf8().constData(),&selected)==0,"Real history read identity");historyReadDevice=static_cast<std::uintmax_t>(selected.st_dev);historyReadInode=static_cast<std::uintmax_t>(selected.st_ino);historyReadEntered=false;historyWriterTrying=false;historyReadAbort=false;historyReadWatch=true;
      std::atomic_bool sawCommitted=false,writerFailed=false;
      std::thread contender([&]{try{
        {std::unique_lock lock(historyReadMutex);if(!historyReadWake.wait_for(lock,std::chrono::seconds(5),[]{return historyReadEntered.load()||historyReadAbort.load();})||historyReadAbort){writerFailed=true;return;}}
        historyWriterTrying=true;historyReadWake.notify_all();auto access=lease.sourceAccess(true);if(!access){writerFailed=true;return;}
        // Real SAME-OFD writer may only enter after the whole sealed commit.
        // No source/epoch setter, forged witness or arbitrary callback grant.
        sawCommitted=rows(a).size()==2;
      }catch(...){writerFailed=true;}});
      auto join=qScopeGuard([&]{historyReadWatch=false;historyReadWorker=false;historyReadAbort=true;historyReadWake.notify_all();if(contender.joinable()){contender.join();}});
      historyReadWorker=true;auto saved=store->appendIntake(snapshot,std::move(*event.receipt));historyReadWorker=false;
      check(saved&&saved->changed&&historyReadEntered&&historyWriterTrying,"Actual contended intake did not publish");
      // Release of the commit's owning gate is complete before this wait.
      contender.join();historyReadWatch=false;
      check(!writerFailed&&sawCommitted&&rows(a).size()==2,"Cooperative write entered the old intake read/write epoch gap");
    });worker.get();
  }
  void existingImportWithoutReceiptAndRecoveryCannotBackfill(){
    Area a;auto worker=std::async(std::launch::async,[&]{auto lease=take(storage::ProfileLease::acquire(a.path()));(void)take(storage::importTorrent(lease,a.metadata.toStdString(),a.payload.toStdString(),true));check(!take(storage::recoverTorrentIntake(lease)),"Unexpected recovery event");auto store=take(storage::DownloadHistoryStore::open(lease));auto snap=take(store->snapshot());check(snap.missing(storage::DownloadHistorySide::Active)&&!QFile::exists(a.profile+"/dlhistorya.config"),"Saved inventory/read/recovery manufactured append");});worker.get();
  }
  void uidCollisionPreservesPreviousRecordAndCommittedIntake(){
    Area a;const auto raw=std::string(reinterpret_cast<const char*>(a.hash.bytes().data()),a.hash.bytes().size());core::DownloadHistoryNewRecord old;old.uid=core::originalDownloadHistoryUid(raw,1900);old.name="collision preserved";old.saveLocation="/previous";auto encoded=core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{old});QVERIFY(encoded);const auto before=encoded->value.source->originalBytes();write(a.profile+"/dlhistoryd.config",before);
    auto worker=std::async(std::launch::async,[&]{auto lease=take(storage::ProfileLease::acquire(a.path()));auto out=imported(a,lease);auto store=take(storage::DownloadHistoryStore::open(lease));check(!store->appendIntake(take(store->snapshot()),std::move(*out.receipt))&&read(a.profile+"/dlhistoryd.config")==before&&!QFile::exists(a.profile+"/dlhistorya.config")&&QFile::exists(a.state()),"UID collision overwrote history or intake");});worker.get();
  }
  void realWindowAdvancedStoppedIntakeAppendsAndReopensWithoutBackfill(){
    Area a;app::SessionBridge bridge;ui::LibraryWindow window;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);connect(&bridge,&app::SessionBridge::intakeDefaultsReady,&window,&ui::LibraryWindow::setIntakeDefaults);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::torrentIntakeReady,&window,&ui::LibraryWindow::setTorrentIntake);connect(&bridge,&app::SessionBridge::torrentIntakeFinished,&window,&ui::LibraryWindow::setTorrentIntakeFinished);
    connect(&window,&ui::LibraryWindow::torrentIntakeRequested,&bridge,[&](auto request){if(!bridge.previewTorrentIntake(request)){window.rejectTorrentIntake(request);}});connect(&window,&ui::LibraryWindow::torrentIntakeSubmitted,&bridge,[&](auto request){if(!bridge.importTorrentFiles(request)){window.rejectTorrentIntakeSubmission(request);}});
    QVERIFY(bridge.openNormal(a.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);QVERIFY(rows(a).empty());window.show();auto* open=window.findChild<QAction*>("classic-open");QVERIFY(open);QTRY_VERIFY(open->isEnabled());open->trigger();auto* dialog=window.findChild<ui::TorrentIntakeDialog*>();QVERIFY(dialog);QTRY_VERIFY(dialog->isVisible());
    auto* source=dialog->findChild<QLineEdit*>("import-source");auto* destination=dialog->findChild<QLineEdit*>("import-destination");auto* mode=dialog->findChild<QComboBox*>("import-start-mode");auto* load=dialog->findChild<QPushButton*>("import-load-files");auto* add=dialog->findChild<QPushButton*>("import-confirm");QVERIFY(source&&destination&&mode&&load&&add);source->setText(a.metadata);destination->setText(a.payload);const auto stopped=mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Stopped));QVERIFY(stopped>=0);mode->setCurrentIndex(stopped);QTRY_VERIFY(load->isEnabled());QTest::mouseClick(load,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(dialog->preview()&&!bridge.busy(),10000);QVERIFY(rows(a).empty());QTRY_VERIFY(add->isEnabled());QTest::mouseClick(add,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==1,10000);auto result=qvariant_cast<ui::TorrentIntakeResultPresentation>(finished.last()[0]);QVERIFY(result&&result->imported&&result->failure.isEmpty()&&!result->started);QCOMPARE(rows(a).size(),std::size_t{1});QVERIFY(!QFile::exists(a.payload+"/fresh.bin"));const auto before=read(a.profile+"/dlhistorya.config");QVERIFY(bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(closed.count()==1&&!bridge.busy(),15000);QVERIFY(closed.last()[2].toBool());QVERIFY(bridge.openNormal(a.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);QCOMPARE(read(a.profile+"/dlhistorya.config"),before);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);
  }
  void historyFailureAfterDurableSimpleImportIsWarningNotRollback(){
    Area a;write(a.profile+"/dlhistorya.config","invalid history retained");app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(a.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);QVERIFY(bridge.torrentImport(a.metadata,a.payload,false));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QVERIFY(QFile::exists(a.state()));QCOMPARE(read(a.profile+"/dlhistorya.config"),std::string("invalid history retained"));QVERIFY(std::any_of(messages.begin(),messages.end(),[](const auto& row){return row[0].toString().contains("The import remains saved");}));auto doc=core::Document::parse(read(a.profile+"/downloads.config"));QVERIFY(doc);auto library=core::readLibrary(*doc);QVERIFY(library&&library->downloads.size()==1);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
  }
};
QTEST_MAIN(DownloadHistoryIntakeTests)
#include "download_history_intake_tests.moc"
