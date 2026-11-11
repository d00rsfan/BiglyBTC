// SPDX-License-Identifier: GPL-2.0-or-later
#include "ordinary_daily_fixture.hpp"
#include <QApplication>
#include "library_window.hpp"
#include <QListWidget>
#include <QAction>
#include <chrono>
#include <zlib.h>
#include <libtorrent/bdecode.hpp>
#include "queued_mainline_scrape.hpp"
#include "encoded_document.hpp"
#include <sys/inotify.h>
#include <unistd.h>
#include <array>
#include <QScopeGuard>
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
lt::entry decodeEntry(const QByteArray& bytes){lt::bdecode_node node;lt::error_code error;lt::bdecode(bytes.constData(),bytes.constData()+bytes.size(),node,error);test::require(!error,"Owned metadata decode");return lt::entry(node);}
QByteArray gzipEntry(const lt::entry& value){const auto raw=test::encoded(value);z_stream stream{};test::require(deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"Owned gzip initialisation");QByteArray bytes(static_cast<qsizetype>(compressBound(raw.size())+64),'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(raw.data()));stream.avail_in=raw.size();stream.next_out=reinterpret_cast<Bytef*>(bytes.data());stream.avail_out=bytes.size();const auto result=deflate(&stream,Z_FINISH);bytes.resize(stream.total_out);const auto ended=deflateEnd(&stream);test::require(result==Z_STREAM_END&&ended==Z_OK,"Owned complete gzip document");return bytes;}
void renameDailyRoot(Profile& profile,const QString& name){
 if(name.isEmpty()){return;}auto state=decodeEntry(test::read(profile.state));state["info"]["name"]=name.toUtf8().toStdString();state["encoding"]="UTF-8";
 const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(state["info"])),QCryptographicHash::Sha1);const auto parsed=core::TorrentHash::parse(std::string_view(digest.constData(),20));test::require(bool(parsed),"Owned renamed torrent identity");
 test::require(QFile::remove(profile.state)&&QFile::remove(profile.archive),"Replace owned generated identity");const auto renamed=QFileInfo(profile.data).absolutePath()+'/'+name;test::require(QFile::rename(profile.data,renamed),"Rename owned Unicode/ampersand root");profile.data=renamed;profile.hash=*parsed;
 profile.state=profile.path+"/active/"+QString::fromStdString(profile.hash.hex())+".dat";profile.archive=profile.path+"/archive/"+QString::fromStdString(profile.hash.hex())+".torrent";
 const auto bytes=QByteArray::fromStdString(test::encoded(state));test::write(profile.state,bytes);test::write(profile.archive,bytes);
 auto inventory=decodeEntry(test::read(profile.path+"/downloads.config"));auto& row=inventory["downloads"].list().front();row["torrent_hash"]=digest.toStdString();row["torrent"]=profile.archive.toStdString();row["save_file"]=name.toUtf8().toStdString();test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
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
 void openedProfilePublishesSavedChatsAfterItsIdentity(){
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-chat-host-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"chat-host",false,true,trackers.url(false),70);
  auto cfg=decodeEntry(test::read(profile.path+"/biglybt.config"));const std::string room="Generated saved host room";
  cfg["azbuddy.dchat.optsmap"]["Public:I5SW4ZLSMF2GKZBAONQXMZLEEBUG643UEBZG633N"]["save"]=1;
  cfg["Plugin.DHT.dht.enabled"]=0;test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(test::encoded(cfg)));
  Host host({});ui::LibraryWindow window;host.bridge.connectChats(&window);auto* open=window.findChild<QAction*>("classic-general-chats");QVERIFY(open);open->trigger();auto* rooms=window.findChild<QListWidget*>("chat-rooms");QVERIFY(rooms);QCOMPARE(rooms->count(),0);
  profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile),qPrintable(host.diagnostic()),15000);
  QTRY_VERIFY2_WITH_TIMEOUT(rooms->count()==1,qPrintable(host.diagnostic()),10000);QCOMPARE(rooms->item(0)->text(),QString::fromStdString(room));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);QTRY_COMPARE_WITH_TIMEOUT(rooms->count(),0,2000);
 }
 void largeCachedLibraryPublishesRowsAndCountProgressBeforeStartingTransfers_data(){QTest::addColumn<bool>("coldCache");QTest::newRow("original-global-cache")<<false;QTest::newRow("after-Java-exit-no-global-cache")<<true;}
 void largeCachedLibraryPublishesRowsAndCountProgressBeforeStartingTransfers(){
  QFETCH(bool,coldCache);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-large-library-XXXXXX"));QVERIFY(root.isValid());
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
   cached["attributes"]["networks"]=lt::entry::list_type{lt::entry(index==0?"I2P":"Public")};cached["opaque.padding"]=std::string(512,'x');cached["au"]=trackers.url(false).toStdString();
   if(index==1){cached["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("I2P"),lt::entry("Tor")};}
   if(index==2){cached["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("Public")};}
   if(index==3){cached.dict().erase("attributes");}
   if(index==4){cached["attributes"]["networks"]=7;}
   if(index==5){auto first=cached;first["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};cache["state"].list().push_back(first);}
   if(coldCache&&cached.dict().contains("attributes")){item["attributes"]=cached["attributes"];test::write(profile.path+"/active/"+hex+".dat",QByteArray::fromStdString(test::encoded(item)));}
   cache["state"].list().push_back(cached);
  }
  test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
  const auto rawCache=test::encoded(cache);z_stream stream{};QCOMPARE(deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY),Z_OK);
  QByteArray compressed(static_cast<qsizetype>(compressBound(rawCache.size())+64),'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(rawCache.data()));stream.avail_in=rawCache.size();stream.next_out=reinterpret_cast<Bytef*>(compressed.data());stream.avail_out=compressed.size();QCOMPARE(deflate(&stream,Z_FINISH),Z_STREAM_END);compressed.resize(stream.total_out);QCOMPARE(deflateEnd(&stream),Z_OK);if(!coldCache){test::write(profile.path+"/active/cache.dat",compressed);}
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
  QVERIFY(host.snapshot()&&!host.snapshot()->inlineDetails);
  qInfo("Large library: %d rows; first presentation %lld ms; ready %lld ms",Count,static_cast<long long>(firstRows),static_cast<long long>(startup.elapsed()));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);
  if(!coldCache){QCOMPARE(test::read(profile.path+"/active/cache.dat"),compressed);}else{QVERIFY(!QFileInfo::exists(profile.path+"/active/cache.dat"));}QCOMPARE(test::read(profile.data),profile.body);
 }
 void startupAdmitsAnIncompleteTorrentBeforeAnOlderSeedPrefix_data(){QTest::addColumn<bool>("forcedSeeds");QTest::newRow("ordinary-prefix")<<false;QTest::newRow("queued-incomplete-before-forced-seeds")<<true;}
 void startupAdmitsAnIncompleteTorrentBeforeAnOlderSeedPrefix(){
  QFETCH(bool,forcedSeeds);
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-startup-order-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"order",false,true,trackers.url(false),0);
  auto baseState=decodeEntry(test::read(profile.state));auto inventory=decodeEntry(test::read(profile.path+"/downloads.config"));const auto base=inventory["downloads"].list().front();inventory["downloads"].list().clear();
  QString expected,expectedSeed;std::set<QByteArray> primaryNames;
  for(int i=0;i<65;++i){
    const bool seed=i<64;const auto name=QStringLiteral("startup-%1.bin").arg(i);auto state=baseState;
    state["info"]=lt::entry::dictionary_type{};state["info"]["length"]=1;state["info"]["name"]=name.toStdString();state["info"]["piece length"]=16384;state["info"]["pieces"]=QCryptographicHash::hash("a",QCryptographicHash::Sha1).toStdString();
    const auto hash=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(state["info"])),QCryptographicHash::Sha1);const auto hex=hash.toHex().toUpper();const auto path=profile.path+"/active/"+QString::fromLatin1(hex)+".dat";
    state["announce"]=("dht://"+hex+".dht/announce").toStdString();state["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};state["attributes"]["resumecomplete"]=seed?2:1;state["resume"]["data"]["valid"]=1;state["resume"]["data"]["resume data"]=std::string(1,seed?'\1':'\0');
    test::write(QFileInfo(profile.data).absolutePath()+'/'+name,seed?QByteArray("a"):QByteArray("x"));test::write(path,QByteArray::fromStdString(test::encoded(state)));
    auto row=base;row["torrent_hash"]=hash.toStdString();row["torrent"]=path.toStdString();row["save_file"]=name.toStdString();row["state"]=!seed&&forcedSeeds?75:0;row["forceStart"]=seed&&forcedSeeds?1:0;row["position"]=seed?i+1:1;row["completedbytes"]=seed?1:0;row["downloaded"]=seed?1:0;row["uploaded"]=0;inventory["downloads"].list().push_back(row);
    primaryNames.insert(hex+".dat");if(!seed){expected=QString::fromLatin1(hex)+".dat";}else if(i==0){expectedSeed=QString::fromLatin1(hex)+".dat";}
  }
  test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
  const int notify=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);QVERIFY(notify>=0);const auto cleanup=qScopeGuard([&]{::close(notify);});QVERIFY(::inotify_add_watch(notify,(profile.path+"/active").toUtf8().constData(),IN_MOVED_TO)>=0);
  Host host({});profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QStringList first;QElapsedTimer elapsed;elapsed.start();
  while(first.size()<2&&elapsed.elapsed()<15000){
    QCoreApplication::processEvents();alignas(inotify_event) std::array<char,32768> bytes{};const auto count=::read(notify,bytes.data(),bytes.size());
    for(std::size_t offset=0;count>0&&offset<static_cast<std::size_t>(count);){const auto* event=reinterpret_cast<const inotify_event*>(bytes.data()+offset);const QByteArray name(event->len?event->name:"");if((event->mask&IN_MOVED_TO)&&primaryNames.contains(name)&&!first.contains(QString::fromLatin1(name))){first.push_back(QString::fromLatin1(name));}offset+=sizeof(inotify_event)+event->len;}
    QTest::qWait(2);
  }
  QVERIFY(first.size()>=2);QCOMPARE(first[0],expected);QCOMPARE(first[1],expectedSeed);QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);
 }
 void queuedTrackerPermissionScanReadsOneFreshGlobalCache(){
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-batch-scrape-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"scan",false,true,trackers.url(false),75);
  auto baseState=decodeEntry(test::read(profile.state));auto inventory=decodeEntry(test::read(profile.path+"/downloads.config"));const auto base=inventory["downloads"].list().front();inventory["downloads"].list().clear();lt::entry cache(lt::entry::dictionary_type{});cache["state"]=lt::entry::list_type{};
  constexpr int Count=300;
  for(int index=0;index<Count;++index){
    auto state=baseState;state["info"]["name"]="queued-"+std::to_string(index)+".bin";const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(state["info"])),QCryptographicHash::Sha1);const auto hex=digest.toHex().toUpper();state["announce"]=("dht://"+hex+".dht/announce").toStdString();state["encoding"]="UTF-8";
    state["attributes"]["parameters"]["dndflags"]=2;state["attributes"]["skipfss"]=0;state["attributes"]["skipbdl"]=0;
    test::write(profile.path+"/active/"+QString::fromLatin1(hex)+".dat",QByteArray::fromStdString(test::encoded(state)));
    auto row=base;row["torrent_hash"]=digest.toStdString();row["save_file"]=state["info"]["name"];row["position"]=index+1;inventory["downloads"].list().push_back(row);
    lt::entry entry(lt::entry::dictionary_type{});entry["hash"]=digest.toStdString();entry["name"]=state["info"]["name"];entry["size"]=Size;entry["fc"]=1;entry["attributes"]=state["attributes"];entry["azp"]=state["azureus_properties"];entry["opaque.padding"]=std::string(16384,'x');cache["state"].list().push_back(entry);
  }
  test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));const auto cachePath=profile.path+"/active/cache.dat";test::write(cachePath,gzipEntry(cache));
  const int notify=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);QVERIFY(notify>=0);const auto cleanup=qScopeGuard([&]{::close(notify);});const int watch=::inotify_add_watch(notify,cachePath.toUtf8().constData(),IN_OPEN|IN_CLOSE_NOWRITE);QVERIFY(watch>=0);
  const int configWatch=::inotify_add_watch(notify,(profile.path+"/biglybt.config").toUtf8().constData(),IN_OPEN|IN_CLOSE_NOWRITE);QVERIFY(configWatch>=0);
  auto lease=storage::ProfileLease::acquire(profile.native());QVERIFY(lease);const auto targets=engine::readSavedMainlineScrapeTargets(*lease);QVERIFY2(targets,qPrintable(targets?QString{}:QString::fromStdString(targets.error().message)));QCOMPARE(targets->size(),std::size_t{Count});
  alignas(inotify_event) std::array<char,16384> events{};int opened=0,configOpened=0;
  for(;;){const auto received=::read(notify,events.data(),events.size());if(received<=0){break;}std::size_t offset=0;while(offset<static_cast<std::size_t>(received)){QVERIFY(static_cast<std::size_t>(received)-offset>=sizeof(inotify_event));const auto* event=reinterpret_cast<const inotify_event*>(events.data()+offset);QVERIFY(sizeof(inotify_event)+event->len<=static_cast<std::size_t>(received)-offset);if(event->mask&IN_OPEN){if(event->wd==watch){++opened;}if(event->wd==configWatch){++configOpened;}}offset+=sizeof(inotify_event)+event->len;}}
  QCOMPARE(opened,1);QCOMPARE(configOpened,1);QCOMPARE(::inotify_rm_watch(notify,watch),0);QCOMPARE(::inotify_rm_watch(notify,configWatch),0);
  // A second call must read changed authoritative attributes, rather than
  // retaining the earlier permission snapshot across operations.
  auto denied=cache["state"].list().front();denied["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};cache["state"].list().push_back(denied);const auto changed=gzipEntry(cache);test::write(cachePath,changed);
  const auto fresh=engine::readSavedMainlineScrapeTargets(*lease);QVERIFY(fresh);QCOMPARE(fresh->size(),std::size_t{Count-1});QCOMPARE(test::read(cachePath),changed);
 }

 void savedDndCompletionUsesSelectedFilesWithOrWithoutGlobalCache_data(){QTest::addColumn<bool>("cached");QTest::newRow("cache-selected-complete")<<true;QTest::newRow("primary-selected-complete")<<false;}
 void savedDndCompletionUsesSelectedFilesWithOrWithoutGlobalCache(){
  QFETCH(bool,cached);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-dnd-classic-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"dnd",false,true,trackers.url(false),70);
  auto state=decodeEntry(test::read(profile.state));auto& info=state["info"];info.dict().erase("length");info["name"]="selected-bundle";lt::entry kept(lt::entry::dictionary_type{}),skipped(lt::entry::dictionary_type{});kept["length"]=Size/2;kept["path"]=lt::entry::list_type{lt::entry("kept.bin")};skipped["length"]=Size/2;skipped["path"]=lt::entry::list_type{lt::entry("skipped.bin")};info["files"]=lt::entry::list_type{kept,skipped};
  state["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L"),lt::entry("L")};state["attributes"]["parameters"]["dndflags"]=3;state["attributes"]["skipfss"]=Size/2;state["attributes"]["skipbdl"]=0;state["attributes"]["resumecomplete"]=1;state["resume"]["data"]["resume data"]=std::string(Size/Piece/2,'\1')+std::string(Size/Piece/2,'\0');
  const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(info)),QCryptographicHash::Sha1);const auto parsed=core::TorrentHash::parse(std::string_view(digest.constData(),20));QVERIFY(parsed);QVERIFY(QFile::remove(profile.state)&&QFile::remove(profile.archive)&&QFile::remove(profile.data));
  profile.data=QFileInfo(profile.data).absolutePath()+"/selected-bundle";test::write(profile.data+"/kept.bin",profile.body.first(Size/2));profile.hash=*parsed;profile.state=profile.path+"/active/"+QString::fromStdString(profile.hash.hex())+".dat";profile.archive=profile.path+"/archive/"+QString::fromStdString(profile.hash.hex())+".torrent";test::write(profile.state,QByteArray::fromStdString(test::encoded(state)));test::write(profile.archive,QByteArray::fromStdString(test::encoded(state)));
  auto inventory=decodeEntry(test::read(profile.path+"/downloads.config"));auto& row=inventory["downloads"].list().front();row["torrent_hash"]=digest.toStdString();row["torrent"]=profile.archive.toStdString();row["save_file"]="selected-bundle";row["completedbytes"]=Size/2;row["file_priorities"]=lt::entry::list_type{lt::entry(1),lt::entry(0)};test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
  if(cached){lt::entry cache(lt::entry::dictionary_type{}),item(lt::entry::dictionary_type{});item["hash"]=digest.toStdString();item["size"]=Size;item["name"]="selected-bundle";item["fc"]=2;item["attributes"]=state["attributes"];item["au"]=state["announce"];cache["state"]=lt::entry::list_type{item};test::write(profile.path+"/active/cache.dat",gzipEntry(cache));}
  auto settings=document(profile.path+"/biglybt.config");QVERIFY(settings.replaceValue("PluginInfo.mldht.enabled","i0e"));test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(settings.serialize()));
  Host host({});profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile),qPrintable(host.diagnostic()),15000);const auto view=host.row(profile);QVERIFY(view&&view->savedSelected);QCOMPARE(view->assumedComplete,std::optional<bool>{true});QCOMPARE(view->completed,std::optional<qint64>{Size/2});QCOMPARE(view->bytes,std::optional<qint64>{Size});QCOMPARE(view->savedSelected->bytes,std::optional<qint64>{Size/2});
  ui::LibraryModel model;QVERIFY(model.apply(host.snapshot()));QCOMPARE(model.completionAt(0),std::optional<bool>{true});QCOMPARE(model.data(model.index(0,3),Qt::DisplayRole).toString(),QStringLiteral("100.0%"));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);QCOMPARE(test::read(profile.data+"/kept.bin"),profile.body.first(Size/2));QVERIFY(!QFileInfo::exists(profile.data+"/skipped.bin"));
 }

 void savedTrackerNameAndDestinationDoNotExposeCredentials_data(){QTest::addColumn<bool>("cached");QTest::addColumn<bool>("preferred");QTest::newRow("primary-host")<<false<<false;QTest::newRow("cache-host")<<true<<false;QTest::newRow("primary-preferred")<<false<<true;QTest::newRow("cache-preferred")<<true<<true;}
 void savedTrackerNameAndDestinationDoNotExposeCredentials(){
  QFETCH(bool,cached);QFETCH(bool,preferred);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-saved-tracker-columns-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"columns",false,true,trackers.url(false),70);renameDailyRoot(profile,QString::fromUtf8("Папка & имя.bin"));
  auto state=decodeEntry(test::read(profile.state));
  state["attributes"]["category"]="Synthetic category";state["attributes"]["comment"]="Synthetic comment & text";state["attributes"]["mergedata"]=17;
  state["attributes"]["parameters"]["stats.download.added.time"]=1700000000000LL;state["attributes"]["complt"]=1600000000000LL;state["attributes"]["parameters"]["stats.download.completed.time"]=1700000010000LL;
  lt::entry tagConfig(lt::entry::dictionary_type{});std::string tagId;unsigned bits=0,value=0;constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  for(const auto byte:profile.hash.bytes()){value=(value<<8)|std::to_integer<unsigned>(byte);bits+=8;while(bits>=5){bits-=5;tagId+=alphabet[(value>>bits)&31];}}
  tagConfig["3"]["1"]["n"]=std::string("Synthetic tag & text");tagConfig["3"]["1"]["o"]=lt::entry::list_type{lt::entry(tagId)};test::write(profile.path+"/tag.config",QByteArray::fromStdString(test::encoded(tagConfig)));
  const std::string secret="a9410b78c3d52e6f8047192abcde3056";const std::string primary="https://user:password@"+secret+".tracker.example/"+secret+"/announce?passkey="+secret;
  const std::string secondary="https://preferred.example/announce?passkey="+secret;state["announce"]=primary;state["announce-list"]=lt::entry::list_type{lt::entry(lt::entry::list_type{lt::entry(primary)}),lt::entry(lt::entry::list_type{lt::entry(secondary)})};
  const auto linked=profile.path+QString::fromUtf8("-linked/Данные & linked.bin");test::write(linked,profile.body);state["attributes"]["filelinks2"]=lt::entry::list_type{lt::entry((QByteArray("0\n")+profile.data.toUtf8()+'\n'+linked.toUtf8()).toStdString())};
  test::write(profile.state,QByteArray::fromStdString(test::encoded(state)));test::write(profile.archive,QByteArray::fromStdString(test::encoded(state)));
  auto config=document(profile.path+"/biglybt.config");QVERIFY(config.replaceValue("PluginInfo.mldht.enabled","i0e"));if(preferred){QVERIFY(config.replaceValue("mtv.trackername.pref.hosts","17:preferred.example"));}test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(config.serialize()));
  QByteArray cacheBytes;
  if(cached){lt::entry cache(lt::entry::dictionary_type{}),item(lt::entry::dictionary_type{});item["hash"]=QByteArray(reinterpret_cast<const char*>(profile.hash.bytes().data()),20).toStdString();item["name"]="display";item["size"]=Size;item["fc"]=1;item["simple"]=1;item["attributes"]=state["attributes"];item["au"]=primary;item["ag"]=state["announce-list"];cache["state"]=lt::entry::list_type{item};cacheBytes=gzipEntry(cache);test::write(profile.path+"/active/cache.dat",cacheBytes);}
  Host host({});profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile),qPrintable(host.diagnostic()),15000);
  const auto row=host.row(profile);QVERIFY(row);QCOMPARE(row->trackerName,preferred?QStringLiteral("preferred.example"):QStringLiteral("tracker.example"));QVERIFY(!row->trackerName.contains(QString::fromStdString(secret)));QVERIFY(!row->trackerName.contains("password"));QCOMPARE(row->savePath,linked);QCOMPARE(row->assumedComplete,std::optional<bool>{true});
  QCOMPARE(row->originalValues[2].toString(),QStringLiteral("Synthetic tag & text"));QCOMPARE(row->originalValues[5].toString(),QStringLiteral("Synthetic category"));
  QCOMPARE(row->originalValues[6].toLongLong(),qint64{1700000000000LL});QCOMPARE(row->originalValues[7].toLongLong(),qint64{1700000010000LL});QCOMPARE(row->originalValues[12].toLongLong(),qint64{17});QCOMPARE(row->originalValues[14].toString(),QStringLiteral("Synthetic comment & text"));
  // A stable primary-only refresh must not reread all piece hashes merely to
  // repaint a tracker cell. A changed file/cache must still invalidate display.
  const int notify=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);QVERIFY(notify>=0);const auto cleanup=qScopeGuard([&]{::close(notify);});const int watch=::inotify_add_watch(notify,profile.state.toUtf8().constData(),IN_OPEN|IN_CLOSE_NOWRITE);QVERIFY(watch>=0);
  const auto revision=host.snapshot()->revision;QVERIFY(host.bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!host.bridge.busy()&&host.snapshot()->revision>revision,15000);
  alignas(inotify_event) std::array<char,4096> notifications{};int opens=0;for(;;){const auto size=::read(notify,notifications.data(),notifications.size());if(size<=0){break;}std::size_t offset=0;while(offset<static_cast<std::size_t>(size)){const auto* event=reinterpret_cast<const inotify_event*>(notifications.data()+offset);if(event->mask&IN_OPEN){++opens;}offset+=sizeof(inotify_event)+event->len;}}
  QCOMPARE(opens,0);QCOMPARE(::inotify_rm_watch(notify,watch),0);
  if(cached){const auto envelope=storage::EncodedDocument::parse(cacheBytes.toStdString(),storage::compatibleConfigLimits());QVERIFY(envelope);auto changed=decodeEntry(QByteArray::fromStdString(envelope->document().serialize()));changed["state"].list().front()["au"]="https://changed.example/announce";changed["state"].list().front()["ag"]=lt::entry::list_type{};cacheBytes=gzipEntry(changed);test::write(profile.path+"/active/cache.dat",cacheBytes);}
  else{state["announce"]="https://changed.example/announce";state["announce-list"]=lt::entry::list_type{};test::write(profile.state,QByteArray::fromStdString(test::encoded(state)));}
  const auto beforeChanged=host.snapshot()->revision;QVERIFY(host.bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!host.bridge.busy()&&host.snapshot()->revision>beforeChanged&&host.row(profile)->trackerName=="changed.example",15000);
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);if(cached){QCOMPARE(test::read(profile.path+"/active/cache.dat"),cacheBytes);}QCOMPARE(test::read(profile.data),profile.body);QCOMPARE(test::read(linked),profile.body);
 }
 void normalRestoredSeedAndPartialUseActualDiscoveryAndTrackers_data(){
  QTest::addColumn<bool>("v6");QTest::addColumn<QString>("logicalName");QTest::newRow("IPv4-cold-warm")<<false<<QString{};QTest::newRow("IPv6-cold-warm")<<true<<QString{};
  QTest::newRow("IPv4-UTF8-direction-mark")<<false<<(QStringLiteral("owned ")+QChar(0x200e)+QStringLiteral(" direction.bin"));
  QTest::newRow("IPv4-Cyrillic")<<false<<QString::fromUtf8("Данные кириллицей.bin");
  QTest::newRow("IPv4-ampersand")<<false<<QStringLiteral("owned & data.bin");
  QTest::newRow("IPv6-Cyrillic-ampersand")<<true<<QString::fromUtf8("Данные & часть.bin");
 }
 void normalRestoredSeedAndPartialUseActualDiscoveryAndTrackers(){
  QFETCH(bool,v6);QFETCH(QString,logicalName);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-daily-host-XXXXXX"));QVERIFY(root.isValid());const auto address=v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;Trackers trackers(address);
  Profile seed(root.path(),"seed",v6,true,trackers.url(false),75,true),partial(root.path(),"partial",v6,false,trackers.url(true));renameDailyRoot(seed,logicalName);renameDailyRoot(partial,logicalName);trackers.allow(seed,partial);QVERIFY(noPortMapping(seed)&&noPortMapping(partial));
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
 void savedForceStartControlsActualStatusAndSurvivesReopen_data(){QTest::addColumn<bool>("complete");QTest::newRow("seeding")<<true;QTest::newRow("downloading")<<false;}
 void savedForceStartControlsActualStatusAndSurvivesReopen(){
  QFETCH(bool,complete);QTemporaryDir root(QStringLiteral("/tmp/biglybtc-daily-forced-XXXXXX"));QVERIFY(root.isValid());Trackers trackers(QHostAddress::LocalHost);Profile profile(root.path(),"forced",false,complete,trackers.url(false),0);trackers.allow(profile,profile);
  auto config=document(profile.path+"/biglybt.config");QVERIFY(config.replaceValue("Peer Source Selection Default.DHT","i0e"));test::write(profile.path+"/biglybt.config",QByteArray::fromStdString(config.serialize()));
  auto inventory=decodeEntry(test::read(profile.path+"/downloads.config"));inventory["downloads"].list().front()["forceStart"]=1;test::write(profile.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
  const auto phase=complete?QStringLiteral("Seeding"):QStringLiteral("Downloading");
  Host host({});profile.releasePorts();QVERIFY(host.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(host.ready(profile)&&host.row(profile)&&host.row(profile)->state.startsWith("Forced ")&&host.row(profile)->state.contains(phase),qPrintable(host.diagnostic()),15000);QVERIFY(forced(profile));
  QVERIFY2(command(host,profile,Action::CancelForceStart),qPrintable(host.diagnostic()));QTRY_VERIFY_WITH_TIMEOUT(host.row(profile)&&host.row(profile)->state.contains(phase)&&!host.row(profile)->state.startsWith("Forced "),15000);QVERIFY(!forced(profile));
  QVERIFY2(command(host,profile,Action::ForceStart),qPrintable(host.diagnostic()));QTRY_VERIFY_WITH_TIMEOUT(host.row(profile)&&host.row(profile)->state.startsWith("Forced ")&&host.row(profile)->state.contains(phase),15000);QVERIFY(forced(profile));
  QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(host),15000);QVERIFY(forced(profile));
  Host reopened({});QVERIFY(reopened.bridge.openNormal(profile.native()));QTRY_VERIFY2_WITH_TIMEOUT(reopened.ready(profile)&&reopened.row(profile)&&reopened.row(profile)->state.startsWith("Forced ")&&reopened.row(profile)->state.contains(phase),qPrintable(reopened.diagnostic()),15000);QVERIFY(forced(profile));
  QVERIFY2(command(reopened,profile,Action::Stop),qPrintable(reopened.diagnostic()));QVERIFY(reopened.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(joinedFinal(reopened),15000);QVERIFY(unknownAndNetworksPreserved(profile));
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
