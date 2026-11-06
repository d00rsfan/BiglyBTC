// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "download_history_page.hpp"
#include "download_history_store.hpp"
#include "torrent_intake.hpp"
#include "torrent_state.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <unistd.h>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
#include <zlib.h>
using namespace biglybtc;
namespace {
std::atomic_bool watching=false,entered=false,held=false;
std::atomic<std::uintmax_t> selectedDevice=0,selectedInode=0;
std::mutex readMutex;std::condition_variable readWake;
void releaseRead(){held=false;watching=false;readWake.notify_all();}
void check(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
template<class T>T take(std::expected<T,storage::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
std::string read(const QString& path){QFile file(path);check(file.open(QIODevice::ReadOnly),"Generated read");return file.readAll().toStdString();}
void write(const QString& path,std::string_view bytes){QFile file(path);check(file.open(QIODevice::WriteOnly)&&file.write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size()),"Generated write");}
struct Area {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-history-view-XXXXXX")};QString profile,payload;
  explicit Area(bool seed=true){
    check(root.isValid(),"Generated area");profile=root.path()+"/profile";payload=root.path()+"/payload.bin";check(QDir().mkpath(profile),"Generated profile");write(payload,"payload preserved");
    auto lease=take(storage::ProfileLease::acquire(profile.toStdString()));
    (void)take(storage::ConfigFile::createMissing(lease,"downloads.config","d9:downloadslee"));
    (void)take(storage::ConfigFile::createMissing(lease,"tables.config","de"));
    // Normal empty profile, all explicit network/discovery/listener flags off.
    core::Document config=std::move(*core::Document::parse("de"));
    for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){check(bool(config.replaceValue(key,"i0e")),"Settings fixture");}
    check(bool(config.replaceValue("Bind IP","9:127.0.0.1"))&&bool(config.replaceValue("Enforce Bind IP","i1e"))&&bool(config.replaceValue("history.opaque","3:raw")),"Settings fixture source");
    (void)take(storage::ConfigFile::createMissing(lease,"biglybt.config",config.serialize()));
    if(seed){for(const auto dead:{false,true}){
      std::string bytes="d14:history.opaque3:raw7:recordslee";
      for(const auto uid:dead?std::vector<std::int64_t>{37}:std::vector<std::int64_t>{17,-9}){
        core::DownloadHistoryNewRecord row;row.uid=uid;row.name=uid==17?"Keep μ":uid==-9?"Remove selected":"Removed saved";row.saveLocation=payload.toStdString();row.size=123;row.addTime=1700000000123;row.completeTime=1700000001123;row.removeTime=dead?1700000002123:0;row.hash=std::string(20,'h');row.hashV2=std::string(32,'v');row.tags={"A","β","A"};
        auto changed=core::editSavedDownloadHistory(bytes,core::DownloadHistoryAppend{row});check(bool(changed),"Generated original history value");bytes=changed->value.source->originalBytes();
      }
      (void)take(storage::ConfigFile::createMissing(lease,dead?"dlhistoryd.config":"dlhistorya.config",bytes));
    }}
  }
  std::filesystem::path path()const{return profile.toStdString();}
};
ui::DownloadHistoryCommandPresentation latest(const QSignalSpy& spy){return qvariant_cast<ui::DownloadHistoryCommandPresentation>(spy.last()[0]);}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window){
  using B=app::SessionBridge;using W=ui::LibraryWindow;window.setOfflinePreview(false);
  QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);
  QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);QObject::connect(&bridge,&B::messageReady,&window,&W::setMessage);
  QObject::connect(&bridge,&B::downloadHistoryCommandFinished,&window,&W::setDownloadHistoryCommandFinished);
  QObject::connect(&window,&W::downloadHistoryCommandRequested,&bridge,[&bridge,&window](ui::DownloadHistoryCommand request){if(!bridge.downloadHistoryCommand(request)){window.rejectDownloadHistoryCommand(request);}});
  window.enableSessionCloseHandshake();QObject::connect(&window,&W::sessionCloseRequested,&bridge,[&bridge,&window]{if(!bridge.closeSession()){window.retrySessionClose();}});
  QObject::connect(&bridge,&B::sessionClosed,&window,[&window](quint64,bool,bool){window.completeSessionClose();});QObject::connect(&bridge,&B::sessionCloseFailed,&window,&W::retrySessionClose);
}
bool answer(ui::LibraryWindow& window,const QString& title,QMessageBox::StandardButton choice,const std::function<void()>& action){
  bool clicked=false;QTimer poll,watchdog;poll.setInterval(1);watchdog.setSingleShot(true);watchdog.setInterval(1500);
  QObject::connect(&poll,&QTimer::timeout,&window,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());box&&box->windowTitle()==title){if(auto* button=box->button(choice)){clicked=true;button->click();}}});
  QObject::connect(&watchdog,&QTimer::timeout,&window,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());box&&box->windowTitle()==title){box->reject();}});
  poll.start();watchdog.start();action();poll.stop();watchdog.stop();return clicked;
}
int row(QTableWidget& table,QString name){for(int at=0;at<table.rowCount();++at){if(table.item(at,0)->text()==name){return at;}}return -1;}
std::string gzip(std::string_view bytes){
  z_stream stream{};check(::deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"Generated gzip init");struct Finish{z_stream* value;~Finish(){::deflateEnd(value);}} finish{&stream};
  std::string output(static_cast<std::size_t>(::compressBound(static_cast<uLong>(bytes.size())))+128,'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));stream.avail_in=static_cast<uInt>(bytes.size());stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=static_cast<uInt>(output.size());check(::deflate(&stream,Z_FINISH)==Z_STREAM_END,"Generated gzip output");output.resize(stream.total_out);return output;
}
std::string opaque(std::string_view source){auto doc=core::Document::parse(std::string(source));check(bool(doc)&&doc->find("history.opaque"),"Unknown root missing");const auto* item=doc->find("history.opaque");return doc->originalBytes().substr(item->begin,item->end-item->begin);}
}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void* bytes,size_t count){
  if(watching){struct stat value{};if(::fstat(fd,&value)==0&&static_cast<std::uintmax_t>(value.st_dev)==selectedDevice&&static_cast<std::uintmax_t>(value.st_ino)==selectedInode){entered=true;std::unique_lock lock(readMutex);readWake.wait_for(lock,std::chrono::seconds(5),[]{return !held.load();});}}
  return __real_read(fd,bytes,count);
}
class DownloadHistoryBridgeTests final:public QObject {
  Q_OBJECT
private slots:
  void ambiguousSavedCacheNeverBecomesResetAuthority_data(){QTest::addColumn<bool>("duplicateField");QTest::newRow("same-hash-conflicting-rows")<<false;QTest::newRow("duplicate-relevant-hash-field")<<true;}
  void ambiguousSavedCacheNeverBecomesResetAuthority(){
    QFETCH(bool,duplicateField);Area area;const auto input=area.root.path()+"/cache-case.torrent";write(input,"d4:infod6:lengthi17e4:name11:payload.bin12:piece lengthi16384e6:pieces20:"+std::string(20,'p')+"ee");
    const auto hash=[&]{auto lease=take(storage::ProfileLease::acquire(area.path()));return take(storage::importTorrent(lease,input.toStdString(),area.root.path().toStdString(),true));}();
    auto config=core::Document::parse(read(area.profile+"/biglybt.config"));QVERIFY(config);QVERIFY(config->replaceValue("Download History Enabled","i1e"));write(area.profile+"/biglybt.config",config->serialize());
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::downloadHistoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);ui::LibraryWindow window;wire(bridge,window);window.show();QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY(!bridge.busy()&&sessions.count()==1);window.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY(!bridge.busy()&&done.count()==1);
    auto* reset=window.downloadHistoryPage()->findChild<QPushButton*>("download-history-reset");QVERIFY(reset&&reset->isEnabled());
    const std::string key="4:hash20:"+std::string(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size());const auto first="d"+key+(duplicateField?key:std::string{})+"10:attributesd11:displayname3:Oneee";const auto second="d"+key+"10:attributesd11:displayname3:Twoee";const auto plainCache="d5:statel"+first+(duplicateField?std::string{}:second)+"ee";
    // Duplicate dictionary keys never reach the factory's selected-field scan:
    // the actual bounded Document parser refuses them first.
    const auto syntax=core::Document::parse(plainCache);QCOMPARE(bool(syntax),!duplicateField);if(duplicateField){QCOMPARE(syntax.error().code,std::string("duplicate_key"));}
    const auto cache=gzip(plainCache);const auto cachePath=area.profile+"/active/cache.dat";
    // Actual conflicting source appears AFTER ordinary startup, so startup
    // cache compaction cannot mask the new factory's ambiguity refusal.
    write(cachePath,cache);const auto active=read(area.profile+"/dlhistorya.config"),dead=read(area.profile+"/dlhistoryd.config"),inventory=read(area.profile+"/downloads.config"),state=read(area.profile+"/active/"+QString::fromStdString(hash.hex())+".dat");
    QVERIFY(answer(window,"Reset download history",QMessageBox::Yes,[&]{reset->click();}));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Rejected);const auto expectedFailure=duplicateField?QStringLiteral("Invalid reconstruction document"):QStringLiteral("Duplicate saved attribute-cache identity");QVERIFY2(latest(done)->failure.contains(expectedFailure),qPrintable(latest(done)->failure));
    QCOMPARE(read(cachePath),cache);QCOMPARE(read(area.profile+"/dlhistorya.config"),active);QCOMPARE(read(area.profile+"/dlhistoryd.config"),dead);QCOMPARE(read(area.profile+"/downloads.config"),inventory);QCOMPARE(read(area.profile+"/active/"+QString::fromStdString(hash.hex())+".dat"),state);QVERIFY(!QFile::exists(area.profile+"/BiglyBTC.download-history.config"));window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QCOMPARE(read(cachePath),cache);
  }
  void actualWindowExplicitResetUsesStoppedBackendAndPreservesUnknowns(){
    Area area;const auto input=area.root.path()+"/stopped.torrent";
    write(input,"d4:infod6:lengthi17e4:name11:payload.bin12:piece lengthi16384e6:pieces20:"+std::string(20,'p')+"ee");
    const auto hash=[&]{auto lease=take(storage::ProfileLease::acquire(area.path()));return take(storage::importTorrent(lease,input.toStdString(),area.root.path().toStdString(),true));}();
    const auto statePath=area.profile+"/active/"+QString::fromStdString(hash.hex())+".dat";
    const auto inventory=read(area.profile+"/downloads.config"),stateBefore=read(statePath),payload=read(area.payload);
    auto original=storage::EncodedDocument::parse(stateBefore);QVERIFY(original);const auto* attrs=original->document().find("attributes");QVERIFY(attrs);const auto* parameters=attrs->find("parameters");
    const auto integer=[](const core::Node* node,std::string_view key){const auto* value=node?node->find(key):nullptr;check(!value||value->type==core::Node::Type::Integer,"Original reset numeric fixture");return value?value->integer:0;};
    const std::string bytes(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size());const auto uid=core::originalDownloadHistoryUid(bytes,integer(attrs,"stats.download.added.time"));
    const auto addTime=integer(parameters,"stats.download.added.time"),completeTime=core::originalDownloadHistoryCompleteTime(integer(attrs,"complt"),integer(parameters,"stats.download.completed.time"));
    core::DownloadHistoryNewRecord old;old.uid=uid;old.hash=bytes;old.name="Outdated saved name";old.saveLocation="/history-only-old-location";old.size=999;old.addTime=addTime;old.completeTime=99;old.tags={"Reset clears original removal-only tags"};
    const auto activePath=area.profile+"/dlhistorya.config",deadPath=area.profile+"/dlhistoryd.config";
    auto appended=core::editSavedDownloadHistory(read(activePath),core::DownloadHistoryAppend{old});QVERIFY(appended);auto history=core::Document::parse(appended->value.source->originalBytes());QVERIFY(history);const auto* records=history->find("records");QVERIFY(records);
    const auto& last=records->children.back();auto row=core::Document::parse(history->originalBytes().substr(last.begin,last.end-last.begin));QVERIFY(row);QVERIFY(row->replaceValue("future-row","l3:onei7ee"));std::string rows="l";
    for(std::size_t n=0;n+1<records->children.size();++n){const auto& value=records->children[n];rows+=history->originalBytes().substr(value.begin,value.end-value.begin);}rows+=row->serialize()+'e';QVERIFY(history->replaceValue("records",rows));write(activePath,history->serialize());
    auto config=core::Document::parse(read(area.profile+"/biglybt.config"));QVERIFY(config);QVERIFY(config->replaceValue("Download History Enabled","i1e"));write(area.profile+"/biglybt.config",config->serialize());const auto settings=read(area.profile+"/biglybt.config"),oldActive=read(activePath),oldDead=read(deadPath);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::downloadHistoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);ui::LibraryWindow window;wire(bridge,window);window.show();
    QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,10000);window.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==1,10000);
    auto* page=window.downloadHistoryPage();QVERIFY(page);auto* reset=page->findChild<QPushButton*>("download-history-reset");auto* table=page->findChild<QTableWidget*>("download-history-table");QVERIFY(reset&&table&&reset->isEnabled());const auto presentation=latest(done);QVERIFY(presentation->enabled&&presentation->resetAvailable);QCOMPARE(table->rowCount(),4);QCOMPARE(read(activePath),oldActive);QCOMPARE(read(deadPath),oldDead);
    QVERIFY(answer(window,"Reset download history",QMessageBox::No,[&]{reset->click();}));QCOMPARE(done.count(),1);QCOMPARE(read(activePath),oldActive);QCOMPARE(read(deadPath),oldDead);
    QVERIFY(answer(window,"Reset download history",QMessageBox::Yes,[&]{reset->click();}));QVERIFY(bridge.busy());QCOMPARE(table->rowCount(),4);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==2,10000);const auto saved=latest(done);QCOMPARE(saved->status,ui::DownloadHistoryStatus::Saved);QVERIFY(saved->changed&&saved->active&&saved->dead);QCOMPARE(saved->active->records.size(),std::size_t{1});QVERIFY(saved->dead->records.empty());
    const auto& actual=saved->active->records.front();QCOMPARE(actual.uid,uid);QCOMPARE(actual.hash,std::optional<std::string>{bytes});QCOMPARE(actual.name,std::string("payload.bin"));QCOMPARE(actual.saveLocation,area.payload.toStdString());QCOMPARE(actual.size,std::int64_t{17});QCOMPARE(actual.addTime,addTime);QCOMPARE(actual.completeTime,completeTime);QVERIFY(actual.tags.empty()&&actual.removeTime==0);
    QCOMPARE(opaque(read(activePath)),opaque(oldActive));QCOMPARE(opaque(read(deadPath)),opaque(oldDead));QVERIFY(read(activePath).find("10:future-rowl3:onei7ee")!=std::string::npos);QCOMPARE(read(area.profile+"/downloads.config"),inventory);QCOMPARE(read(statePath),stateBefore);QCOMPARE(read(area.profile+"/biglybt.config"),settings);QCOMPARE(read(area.payload),payload);QVERIFY(!QFile::exists(area.profile+"/BiglyBTC.download-history.config"));
    const auto resetActive=read(activePath),resetDead=read(deadPath);QVERIFY(answer(window,"Reset download history",QMessageBox::Yes,[&]{reset->click();}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==3,10000);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Unchanged);QCOMPARE(read(activePath),resetActive);QCOMPARE(read(deadPath),resetDead);
    ui::DownloadHistoryCommand stale;stale.session=presentation->session;stale.request=100;stale.generation=presentation->generation;stale.kind=ui::DownloadHistoryKind::Reset;QVERIFY(bridge.downloadHistoryCommand(stale));QTRY_VERIFY(!bridge.busy()&&done.count()==4);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Rejected);QCOMPARE(read(activePath),resetActive);QCOMPARE(read(deadPath),resetDead);
    window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);ui::LibraryWindow reopened;wire(bridge,reopened);reopened.show();QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY(!bridge.busy()&&sessions.count()==2);reopened.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY(!bridge.busy()&&done.count()==5);QCOMPARE(latest(done)->active->records.size(),std::size_t{1});QCOMPARE(read(activePath),resetActive);QCOMPARE(read(deadPath),resetDead);reopened.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);
  }
  void actualWindowReadsErasesClearsNavigatesAndReopens(){
    Area area;const auto active=area.profile+"/dlhistorya.config",dead=area.profile+"/dlhistoryd.config";
    const auto oldActive=read(active),oldDead=read(dead),inventory=read(area.profile+"/downloads.config");
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::downloadHistoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    ui::LibraryWindow window;wire(bridge,window);QSignalSpy requests(&window,&ui::LibraryWindow::downloadHistoryCommandRequested);window.show();
    QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,10000);
    auto* open=window.findChild<QAction*>("classic-download-history");QVERIFY(open);QTRY_VERIFY(open->isEnabled());open->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==1,10000);
    auto* page=window.downloadHistoryPage();QVERIFY(page&&window.mainTabs().currentWidget()==page);auto* table=page->findChild<QTableWidget*>("download-history-table");auto* erase=page->findChild<QPushButton*>("download-history-erase");auto* clear=page->findChild<QPushButton*>("download-history-clear");QVERIFY(table&&erase&&clear);
    QCOMPARE(table->rowCount(),3);QVERIFY(!latest(done)->enabled&&latest(done)->writable);QCOMPARE(read(active),oldActive);QCOMPARE(read(dead),oldDead);
    const auto capture=qEnvironmentVariable("BIGLYBTC_HISTORY_VIEW_CAPTURE_FILE");
    if(!capture.isEmpty()){
      const QFileInfo info(capture);QVERIFY(info.isAbsolute()&&!info.exists()&&!info.isSymLink());
      QVERIFY(info.dir().canonicalPath()==info.dir().absolutePath());
      QVERIFY(window.isVisible()&&!window.isMinimized()&&page->isVisible());
      QCOMPARE(window.mainTabs().currentWidget(),page);QCOMPARE(page->session(),latest(done)->session);
      QFile output(capture);QVERIFY(output.open(QIODevice::WriteOnly|QIODevice::NewOnly));
      QVERIFY(window.grab().save(&output,"PNG"));QVERIFY(output.flush());output.close();
      QCOMPARE(table->rowCount(),3);QCOMPARE(read(active),oldActive);QCOMPARE(read(dead),oldDead);
    }
    const auto at=row(*table,QStringLiteral("Remove selected"));QVERIFY(at>=0);table->setCurrentCell(at,0);table->selectRow(at);QVERIFY(erase->isEnabled());
    const auto before=requests.count();QVERIFY(answer(window,"Remove history record",QMessageBox::No,[&]{erase->click();}));QCOMPARE(requests.count(),before);QCOMPARE(read(active),oldActive);
    QVERIFY(answer(window,"Remove history record",QMessageBox::Yes,[&]{erase->click();}));QVERIFY(bridge.busy());QCOMPARE(table->rowCount(),3);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==2,10000);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Saved);QCOMPARE(table->rowCount(),2);QCOMPARE(latest(done)->uid,qint64{-9});QCOMPARE(opaque(read(active)),opaque(oldActive));QCOMPARE(read(dead),oldDead);
    // Ctrl+1 is a real WindowShortcut. Restore and verify its ordinary input
    // context after the two modal confirmations, as the existing shortcut
    // lifecycle tests do; direct key delivery alone does not activate a window.
    QTRY_VERIFY(QApplication::activeModalWidget()==nullptr);window.activateWindow();QTRY_VERIFY(window.isActiveWindow());
    table->setFocus();QTRY_VERIFY(table->hasFocus());
    auto* myTorrents=window.findChild<QAction*>("classic-my-torrents");QVERIFY(myTorrents&&myTorrents->isEnabled());
    QSignalSpy navigation(myTorrents,&QAction::triggered);QTest::keyClick(table,Qt::Key_1,Qt::ControlModifier);QTRY_COMPARE(navigation.count(),1);
    QTRY_COMPARE(window.mainTabs().currentWidget()->objectName(),QStringLiteral("classic-my-torrents-page"));open->trigger();QCOMPARE(window.downloadHistoryPage(),page);QCOMPARE(window.mainTabs().currentWidget(),page);QCOMPARE(done.count(),2);
    QVERIFY(answer(window,"Clear saved history",QMessageBox::Yes,[&]{clear->click();}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&done.count()==3,10000);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Saved);QCOMPARE(table->rowCount(),0);
    QCOMPARE(opaque(read(active)),opaque(oldActive));QCOMPARE(opaque(read(dead)),opaque(oldDead));QCOMPARE(read(area.profile+"/downloads.config"),inventory);QCOMPARE(read(area.payload),std::string("payload preserved"));
    const auto stalePacket=latest(done);QPointer<ui::DownloadHistoryPage> retired=page;
    auto* closeTab=window.findChild<QAction*>("classic-close-tab");QVERIFY(closeTab&&closeTab->isEnabled());closeTab->trigger();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTRY_VERIFY(retired.isNull());
    open->trigger();QVERIFY(window.downloadHistoryPage());window.setDownloadHistoryCommandFinished(stalePacket);QTRY_VERIFY(!bridge.busy()&&done.count()==4);QCOMPARE(window.downloadHistoryPage()->findChild<QTableWidget*>("download-history-table")->rowCount(),0);
    window.close();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&closed.count()==1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());{auto lease=storage::ProfileLease::acquire(area.path());QVERIFY(lease);}
    // The old view may retain owning history values but cannot retain the OFD.
    QObject::disconnect(&bridge,nullptr,&window,nullptr);QObject::disconnect(&window,nullptr,&bridge,nullptr);
    ui::LibraryWindow fresh;wire(bridge,fresh);fresh.show();QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY(!bridge.busy()&&sessions.count()==2);fresh.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY(!bridge.busy()&&done.count()==5);QCOMPARE(fresh.downloadHistoryPage()->findChild<QTableWidget*>("download-history-table")->rowCount(),0);fresh.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);
  }
  void previewMissingCorruptAndRecoveryNeverCreateOrWrite_data(){QTest::addColumn<int>("mode");QTest::newRow("missing")<<0;QTest::newRow("preview-existing-disabled")<<1;QTest::newRow("corrupt")<<2;QTest::newRow("intent-preserved")<<3;}
  void previewMissingCorruptAndRecoveryNeverCreateOrWrite(){
    QFETCH(int,mode);Area area(mode!=0);if(mode==2){write(area.profile+"/dlhistorya.config","corrupt preserved");}if(mode==3){write(area.profile+"/BiglyBTC.download-history.config","opaque review candidate");}
    const auto primary=area.profile+"/dlhistorya.config";const auto before=QFileInfo::exists(primary)?std::optional{read(primary)}:std::nullopt;
    app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.show();QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::downloadHistoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(area.path(),0));QTRY_VERIFY(!bridge.busy()&&sessions.count()==1);window.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY(!bridge.busy()&&done.count()==1);
    auto packet=latest(done);QVERIFY(!packet->writable);if(mode==2){QCOMPARE(packet->status,ui::DownloadHistoryStatus::Rejected);}else{QCOMPARE(packet->status,ui::DownloadHistoryStatus::Loaded);}if(mode==0){QCOMPARE(packet->readState,ui::DownloadHistoryReadState::Missing);}if(mode==3){QCOMPARE(packet->review,ui::DownloadHistoryReview::Intent);}
    auto* page=window.downloadHistoryPage();QVERIFY(page);QVERIFY(!page->findChild<QPushButton*>("download-history-clear")->isEnabled());
    ui::DownloadHistoryCommand forged;forged.session=sessions.last()[0].toULongLong();forged.request=100;forged.generation=packet->generation;forged.kind=ui::DownloadHistoryKind::Clear;QVERIFY(bridge.downloadHistoryCommand(forged));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Rejected);
    QCOMPARE(QFileInfo::exists(primary),before.has_value());if(before){QCOMPARE(read(primary),*before);}QCOMPARE(read(area.payload),std::string("payload preserved"));window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
  }
  void actualStaleSourceCancelledReadAndClosedProfileRefuse(){
    Area area;const auto path=area.profile+"/dlhistorya.config";app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.show();const auto release=qScopeGuard([]{releaseRead();});
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::downloadHistoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(area.path()));QTRY_VERIFY(!bridge.busy()&&sessions.count()==1);window.findChild<QAction*>("classic-download-history")->trigger();QTRY_VERIFY(!bridge.busy()&&done.count()==1);auto packet=latest(done);const auto original=read(path);
    // Actual named-inode change, same bytes: a stale value packet cannot bless it.
    QVERIFY(QFile::rename(path,path+".foreign"));write(path,original);ui::DownloadHistoryCommand stale;stale.session=packet->session;stale.request=100;stale.generation=packet->generation;stale.kind=ui::DownloadHistoryKind::Erase;stale.uid=17;
    QVERIFY(bridge.downloadHistoryCommand(stale));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Rejected);QCOMPARE(read(path),original);
    ui::DownloadHistoryCommand reload;reload.session=packet->session;reload.request=101;reload.kind=ui::DownloadHistoryKind::Reload;
    struct stat info{};QVERIFY(::stat(path.toStdString().c_str(),&info)==0);selectedDevice=info.st_dev;selectedInode=info.st_ino;entered=false;held=true;watching=true;
    QVERIFY(bridge.downloadHistoryCommand(reload));QTRY_VERIFY_WITH_TIMEOUT(entered.load(),10000);bridge.cancel();releaseRead();QTRY_VERIFY(!bridge.busy()&&done.count()==3);QCOMPARE(latest(done)->status,ui::DownloadHistoryStatus::Cancelled);QCOMPARE(read(path),original);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);reload.request=102;QVERIFY(!bridge.downloadHistoryCommand(reload));QCOMPARE(read(path),original);
  }
};
QTEST_MAIN(DownloadHistoryBridgeTests)
#include "download_history_bridge_tests.moc"
