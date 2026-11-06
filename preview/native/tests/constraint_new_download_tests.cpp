// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "../app/new_download_events.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;
std::string encoded(const libtorrent::entry& e){std::string out;libtorrent::bencode(std::back_inserter(out),e);return out;}
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.flush()){throw std::runtime_error("Generated fixture write");}}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated fixture read");}return f.readAll();}
core::TorrentHash hash(const std::string& info){const auto raw=QCryptographicHash::hash(QByteArray::fromStdString(info),QCryptographicHash::Sha1);auto out=core::TorrentHash::parse(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));if(!out){throw std::runtime_error("Generated raw hash");}return *out;}
libtorrent::entry metainfo(const char* name){
  libtorrent::entry doc(libtorrent::entry::dictionary_type{});auto& info=doc["info"];
  info["length"]=1;info["name"]=name;info["piece length"]=16384;info["pieces"]=QCryptographicHash::hash(QByteArray("x"),QCryptographicHash::Sha1).toStdString();
  doc["attributes"]["networks"]=libtorrent::entry::list_type{libtorrent::entry("Public")};doc["attributes"]["storetypes"]=libtorrent::entry::list_type{libtorrent::entry("L")};
  doc["attributes"]["parameters"]["stats.download.added.time"]=123;doc["attributes"]["parameters"]["future"]="keep";doc["future-root"]="keep";return doc;
}
struct Fixture {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-new-download-XXXXXX")};QString profile,metadata,destination;
  core::TorrentHash oldHash,newHash;
  Fixture(){
    if(!root.isValid()){throw std::runtime_error("Generated area");}profile=root.path()+"/profile";metadata=root.path()+"/new.torrent";destination=root.path()+"/new-payload";
    QDir().mkpath(profile+"/active");QDir().mkpath(root.path()+"/old-payload");
    auto old=metainfo("old.bin");old["attributes"]["parameters"]["stats.download.added.time"]=QDateTime::currentMSecsSinceEpoch();oldHash=hash(encoded(old["info"]));write(profile+"/active/"+QString::fromStdString(oldHash.hex())+".dat",QByteArray::fromStdString(encoded(old)));write(root.path()+"/old-payload/old.bin",QByteArray("x"));
    auto fresh=metainfo("new.bin");newHash=hash(encoded(fresh["info"]));write(metadata,QByteArray::fromStdString(encoded(fresh)));
    libtorrent::entry inventory(libtorrent::entry::dictionary_type{}),row(libtorrent::entry::dictionary_type{});const auto raw=oldHash.bytes();
    row["torrent_hash"]=std::string(reinterpret_cast<const char*>(raw.data()),raw.size());row["torrent"]=(profile+"/active/"+QString::fromStdString(oldHash.hex())+".dat").toStdString();
    row["save_dir"]=(root.path()+"/old-payload").toStdString();row["save_file"]="old.bin";row["state"]=70;row["persistent"]=1;row["position"]=1;row["downloaded"]=0;row["uploaded"]=0;row["completedbytes"]=0;row["file_priorities"]=libtorrent::entry::list_type{libtorrent::entry(0)};inventory["downloads"]=libtorrent::entry::list_type{row};
    write(profile+"/downloads.config",QByteArray::fromStdString(encoded(inventory)));write(profile+"/tables.config",QByteArray("de"));
    libtorrent::entry tags(libtorrent::entry::dictionary_type{});tags["3"]["1"]["n"]="New only";tags["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("size>0"),libtorrent::entry("am=3;")};tags["3"]["1"]["o"]=libtorrent::entry::list_type{};write(profile+"/tag.config",QByteArray::fromStdString(encoded(tags)));
    libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;settings["TCP.Listen.Port"]=6881;settings["UDP.Listen.Port"]=6881;settings["IPV6 Enable Support"]=0;
    for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}
    settings["Tag Auto Full Reapply Period Secs"]=0;write(profile+"/biglybt.config",QByteArray::fromStdString(encoded(settings)));
  }
  std::filesystem::path path()const{return profile.toStdString();}
};
ui::ManualTagCommandPresentation latest(const QSignalSpy& s){return s.empty()?ui::ManualTagCommandPresentation{}:qvariant_cast<ui::ManualTagCommandPresentation>(s.last()[0]);}
std::size_t members(const QString& path){auto t=core::readSavedDownloadTags(read(path).toStdString());if(!t||t->tags.empty()){return 999;}return t->tags.front().uniqueHashes.size();}
std::atomic_bool watched=false,held=false,entered=false;std::atomic<std::uintmax_t> device=0,inode=0;std::mutex mutex;std::condition_variable wake;
void release(){held=false;wake.notify_all();}
}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void* out,size_t size){
  if(watched){struct stat s{};if(::fstat(fd,&s)==0&&static_cast<std::uintmax_t>(s.st_dev)==device&&static_cast<std::uintmax_t>(s.st_ino)==inode){entered=true;std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::seconds(10),[]{return !held.load();});}}
  return __real_read(fd,out,size);
}
class ConstraintNewDownloadTests final:public QObject {
  Q_OBJECT
private slots:
  void boundedAgeDuplicateExpiryAndFairProgressAreValuesNotAuthority(){
    using Events=app::detail::NewDownloadEvents;using Added=Events::Added;using Clock=Events::Clock;
    const auto now=Clock::time_point{};const auto one=hash("one"),two=hash("two");Events events;
    QVERIFY(!Events::recent(0,1000000));QVERIFY(Events::recent(1000000,1299999));QVERIFY(!Events::recent(1000000,1300000));QVERIFY(Events::recent(1000001,1000000));
    QCOMPARE(events.record(one,1000000,1000000,now,1),Added::Recorded);const auto end=events.front().end;
    QCOMPARE(events.record(one,1000000,1000001,now+std::chrono::milliseconds(1),1),Added::Duplicate);QCOMPARE(events.front().end,end);QCOMPARE(events.record(two,1000000,1000001,now,1),Added::Unavailable);QCOMPARE(events.record(one,1000000,1000000,now,0),Added::Unavailable);
    events.front().rules=std::make_shared<const app::detail::NewDownloadRules>();events.finishTurn();QVERIFY(events.empty());
    QCOMPARE(events.record(one,1000000,1000100,now+std::chrono::milliseconds(100),1),Added::Duplicate);
    QCOMPARE(events.record(two,1000000,1000000,now,2),Added::Recorded);QCOMPARE(events.record(one,1000001,1000001,now,3),Added::Recorded);events.finishTurn();QVERIFY(events.front().hash==one);
    events.expire(now+std::chrono::milliseconds(300000));QVERIFY(events.empty());
    QCOMPARE(events.record(one,1000000,1300000,now,4),Added::Old);QCOMPARE(events.record(one,1000000,1000000,Clock::time_point::max(),5),Added::Unavailable);
    events.clear();for(std::size_t n=0;n<Events::Maximum;++n){std::string raw(20,'\0');for(unsigned b=0;b<8;++b){raw[b]=static_cast<char>(static_cast<std::uint64_t>(n)>>(b*8));}auto key=core::TorrentHash::parse(raw);QVERIFY(key);QCOMPARE(events.record(*key,1000000,1000000,now,n+1),Added::Recorded);}
    QCOMPARE(events.record(hash("overflow"),1000000,1000000,now,Events::Maximum+1),Added::Full);events.expire(now+std::chrono::milliseconds(300000));QVERIFY(events.empty());QCOMPARE(events.record(one,1300000,1300000,now+std::chrono::milliseconds(300000),Events::Maximum+2),Added::Recorded);
  }
  void actualDurableIntakeTagsOnlyNewDownloadOnceAndReopenDoesNotRecreateEvent(){
    Fixture f;app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),rows(&bridge,&app::SessionBridge::snapshotReady),done(&bridge,&app::SessionBridge::manualTagCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}QVERIFY(idle);QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=2,4000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    QVERIFY(bridge.torrentImport(f.metadata,f.destination,false));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QTRY_COMPARE_WITH_TIMEOUT(members(f.profile+"/tag.config"),std::size_t{1},10000);
    auto tags=core::readSavedDownloadTags(read(f.profile+"/tag.config").toStdString());QVERIFY(tags);QVERIFY(tags->tags.front().uniqueHashes.front()==f.newHash);
    auto state=core::Document::parse(read(f.profile+"/active/"+QString::fromStdString(f.newHash.hex())+".dat").toStdString());QVERIFY(state);const auto* params=state->find("attributes")->find("parameters");QVERIFY(params&&params->find("stats.download.added.time")->integer>123);QCOMPARE(params->find("future")->bytes,std::string("keep"));
    const auto saved=read(f.profile+"/tag.config");QVERIFY(bridge.torrentImport(f.metadata,f.destination,false));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QCOMPARE(read(f.profile+"/tag.config"),saved);
    ui::ManualTagCommand command;command.session=sessions.last()[0].toULongLong();command.request=1;QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==1);QVERIFY(latest(done));
    const auto view=qvariant_cast<ui::Presentation>(rows.last()[0]);const auto found=std::find_if(view->items.begin(),view->items.end(),[&](const auto& row){return row.hash==QString::fromStdString(f.newHash.hex());});QVERIFY(found!=view->items.end());
    command.kind=ui::ManualTagCommandKind::RemoveMember;command.request=2;command.generation=latest(done)->generation;command.tagId=1;command.member={found->hash,view->revision};QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    const auto before=turns.count();QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=before+2,4000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());QVERIFY(bridge.openNormal(f.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);
    const auto reopened=turns.count();QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=reopened+2,4000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);QVERIFY(closed.last()[2].toBool());
  }
  void propertyChangeOnRecentExistingRowsDoesNotManufactureCreation(){
    Fixture f;app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),done(&bridge,&app::SessionBridge::manualTagCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(f.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}QVERIFY(idle);QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=2,4000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    ui::ManualTagCommand command;command.session=sessions.last()[0].toULongLong();command.request=1;QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==1);
    command.kind=ui::ManualTagCommandKind::SetConstraint;command.request=2;command.generation=latest(done)->generation;command.tagId=1;command.constraintExpression="true";command.constraintOptions="am=3;";QVERIFY(bridge.manualTagCommand(command));QTRY_VERIFY(!bridge.busy()&&done.count()==2);QCOMPARE(latest(done)->status,ui::ManualTagCommandStatus::Saved);
    const auto before=turns.count();QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=before+2,4000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
  void changedOriginalAddedTimeWithdrawsPendingNewnessWithoutTagWrites(){
    Fixture f;app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(f.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}QVERIFY(idle);idle->stop();
    QVERIFY(bridge.torrentImport(f.metadata,f.destination,false));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    const auto file=f.profile+"/active/"+QString::fromStdString(f.newHash.hex())+".dat";auto state=core::Document::parse(read(file).toStdString());QVERIFY(state);const auto* a=state->find("attributes");QVERIFY(a);auto attrs=core::Document::parse(state->originalBytes().substr(a->begin,a->end-a->begin));QVERIFY(attrs);const auto* p=attrs->find("parameters");QVERIFY(p);auto parameters=core::Document::parse(attrs->originalBytes().substr(p->begin,p->end-p->begin));QVERIFY(parameters);QVERIFY(parameters->setInteger("stats.download.added.time",1));QVERIFY(attrs->replaceValue("parameters",parameters->serialize()));QVERIFY(state->replaceValue("attributes",attrs->serialize()));write(file,QByteArray::fromStdString(state->serialize()));
    idle->start();QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=3,5000);QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
  void cancellationBeforeActualImportNeverPublishesANewDownloadEvent(){
    Fixture f;app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(f.path()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    struct stat s{};QVERIFY(::stat(f.metadata.toStdString().c_str(),&s)==0);device=s.st_dev;inode=s.st_ino;entered=false;held=true;watched=true;const auto cleanup=qScopeGuard([]{watched=false;release();});
    QVERIFY(bridge.torrentImport(f.metadata,f.destination,false));QTRY_VERIFY_WITH_TIMEOUT(entered.load(),10000);bridge.cancel();release();watched=false;QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    const auto inventory=core::Document::parse(read(f.profile+"/downloads.config").toStdString());QVERIFY(inventory);QCOMPARE(inventory->find("downloads")->children.size(),std::size_t{1});QCOMPARE(members(f.profile+"/tag.config"),std::size_t{0});QVERIFY(!QFile::exists(f.profile+"/active/"+QString::fromStdString(f.newHash.hex())+".dat"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
};
QTEST_MAIN(ConstraintNewDownloadTests)
#include "constraint_new_download_tests.moc"
