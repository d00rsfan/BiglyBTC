#include "profile.hpp"
#include "session_bridge.hpp"
#include "manual_tag_policy.hpp"
#include "tag_constraint_worker.hpp"
#include <QTimer>
#include <future>
#include "library_window.hpp"
#include "manual_tag_view.hpp"
#include "manual_tag_editor.hpp"
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QBuffer>
#include <QFile>
#include <QPixmap>
#include <QTableView>
#include <cerrno>
#include <fcntl.h>

#include "owned_engine_fixture.hpp"
#include "biglybtc/manual_tag_statistics.hpp"

#include <QApplication>
#include <QPointer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QScopeGuard>
#include <condition_variable>
#include <mutex>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
std::atomic<bool> watched{false};
std::atomic<std::uintmax_t> watchedDevice{0}, watchedInode{0};
std::atomic<unsigned> tagReads{0};
std::atomic<bool> holdTagRead{false},heldTagReadEntered{false};
std::mutex tagReadMutex;std::condition_variable tagReadWake;
struct Watch {
  explicit Watch(const QString& filename) {
    struct stat info{}; test::require(::stat(filename.toStdString().c_str(), &info) == 0, "Cannot watch owned tag file");
    watchedDevice = static_cast<std::uintmax_t>(info.st_dev); watchedInode = static_cast<std::uintmax_t>(info.st_ino);
    tagReads = 0; watched = true;
  }
  ~Watch() { watched = false; }
};
struct stat tagStamp(const QString& path) {
  struct stat value{}; test::require(::stat(path.toStdString().c_str(), &value) == 0, "Cannot stamp owned tag file");
  return value;
}
bool sameTag(const struct stat& before, const QString& path) {
  const auto after = tagStamp(path);
  return before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
      && before.st_mode == after.st_mode && before.st_nlink == after.st_nlink
      && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
      && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}
std::string base32(const core::TorrentHash& hash) {
  constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::uint32_t pending = 0; unsigned bits = 0; std::string result;
  for (const auto byte : hash.bytes()) {
    pending = (pending << 8) | std::to_integer<unsigned char>(byte); bits += 8;
    while (bits >= 5) { bits -= 5; result += alphabet[(pending >> bits) & 31]; }
  }
  if (bits) { result += alphabet[(pending << (5 - bits)) & 31]; }
  return result;
}
QByteArray tags(const core::TorrentHash& target) {
  libtorrent::entry config(libtorrent::entry::dictionary_type{});
  config["3"]["1"]["n"] = "Actual";
  config["3"]["1"]["o"] = libtorrent::entry::list_type{libtorrent::entry(base32(target))};
  config["3"]["2"]["n"] = "Empty";
  config["3"]["2"]["o"] = libtorrent::entry::list_type{};
  const auto unknown = core::TorrentHash::parse(std::string(20, 'z'));
  test::require(unknown.has_value(), "Cannot prepare unresolved member");
  config["3"]["3"]["n"] = "Unresolved";
  config["3"]["3"]["o"] = libtorrent::entry::list_type{libtorrent::entry(base32(*unknown))};
  config["3"]["c"]["opaque"] = std::string("\0\xffQ", 3);
  config["77"]["1"]["n"] = "Unadopted other tag type";
  config["unknown-root"] = std::string("\0\xffR", 3);
  return QByteArray::fromStdString(test::encoded(config));
}
ui::ManualTagStatisticsPresentation latest(const QSignalSpy& spy) {
  return spy.empty() ? ui::ManualTagStatisticsPresentation{}
      : qvariant_cast<ui::ManualTagStatisticsPresentation>(spy.last().at(0));
}
ui::NativeSessionPresentation native(const QSignalSpy& spy) {
  return spy.empty() ? ui::NativeSessionPresentation{}
      : qvariant_cast<ui::NativeSessionPresentation>(spy.last().at(0));
}
ui::Target target(const QSignalSpy& spy) {
  const auto rows = qvariant_cast<ui::Presentation>(spy.last().at(0));
  test::require(rows && rows->items.size() == 1, "Expected one generated library row");
  return {.hash = rows->items.front().hash, .generation = rows->revision};
}
bool activePositive(const ui::ManualTagStatisticsPresentation& p) {
  return p && p->readState == ui::ManualTagReadState::Ready && p->traffic.current
      && p->traffic.state == core::ManualTagTrafficState::Active && p->traffic.target
      && p->traffic.target->payloadDownloadRate > 0;
}
bool sawPositive(const QSignalSpy& spy) {
  return std::any_of(spy.begin(), spy.end(), [](const auto& row) {
    return activePositive(qvariant_cast<ui::ManualTagStatisticsPresentation>(row.at(0)));
  });
}
// Optional owned output only. A directory descriptor anchors exclusive0600
// writes; canonical path/owner/mode/identity checks reject aliases and links.
class CaptureDirectory final {
public:
  explicit CaptureDirectory(QString path) : path_(std::move(path)) {
    const QFileInfo info(path_);
    if (!info.isAbsolute() || !info.isDir() || info.isSymLink() || info.canonicalFilePath() != path_) {
      throw std::runtime_error("Manual Tag capture directory must be canonical and private");
    }
    const auto encoded = QFile::encodeName(path_);
    descriptor_ = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!valid()) {
      if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); descriptor_ = -1; }
      throw std::runtime_error("Manual Tag capture directory must be owned0700");
    }
  }
  ~CaptureDirectory() { if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); } }
  CaptureDirectory(const CaptureDirectory&) = delete;
  CaptureDirectory& operator=(const CaptureDirectory&) = delete;
  bool save(ui::ManualTagStatisticsView& view, const ui::ManualTagStatisticsPresentation& expected,
      const char* basename) const {
    if (view.presentation() != expected) { return false; }
    QByteArray bytes; QBuffer stream(&bytes);
    if (!stream.open(QIODevice::WriteOnly) || !view.grab().save(&stream, "PNG") || !valid()
        || view.presentation() != expected || !view.isVisible() || view.isMinimized()) { return false; }
    const int output = ::openat(descriptor_, basename, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output < 0) { return false; }
    bool success = true; qsizetype offset = 0;
    while (offset < bytes.size()) {
      const auto written = ::write(output, bytes.constData() + offset, static_cast<std::size_t>(bytes.size() - offset));
      if (written < 0 && errno == EINTR) { continue; }
      if (written <= 0) { success = false; break; }
      offset += written;
    }
    if (success && ::fsync(output) != 0) { success = false; }
    if (::close(output) != 0) { success = false; }
    if (success && ::fsync(descriptor_) != 0) { success = false; }
    return success;
  }
private:
  bool valid() const {
    struct stat held{}, named{}; const auto encoded = QFile::encodeName(path_);
    return descriptor_ >= 0 && ::fstat(descriptor_, &held) == 0 && ::lstat(encoded.constData(), &named) == 0
        && S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode) && held.st_uid == ::geteuid()
        && (held.st_mode & 0777) == 0700 && held.st_dev == named.st_dev && held.st_ino == named.st_ino;
  }
  QString path_;
  int descriptor_ = -1;
};
bool positiveTagRendered(ui::ManualTagStatisticsView& view, const ui::ManualTagStatisticsPresentation& packet) {
  if (!activePositive(packet) || view.presentation() != packet || !view.isVisible() || view.isMinimized()) { return false; }
  const auto* table = view.findChild<QTableView*>(QStringLiteral("manual-tag-table"));
  if (!table || !table->model()) { return false; }
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const auto name = table->model()->index(row, 0);
    const auto down = table->model()->index(row, 3);
    if (name.data().toString() == QStringLiteral("Actual")
        && down.data(Qt::UserRole).isValid() && down.data(Qt::UserRole).toLongLong() == packet->traffic.target->downloadRate
        && down.data(Qt::UserRole).toLongLong() > 0) { return true; }
  }
  return false;
}
} // namespace

extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __wrap_read(int descriptor, void* bytes, size_t size) {
  if (watched.load()) {
    struct stat info{};
    if (::fstat(descriptor, &info) == 0
        && static_cast<std::uintmax_t>(info.st_dev) == watchedDevice.load()
        && static_cast<std::uintmax_t>(info.st_ino) == watchedInode.load()) {
      ++tagReads;
      if(holdTagRead.load()){
        std::unique_lock lock(tagReadMutex);heldTagReadEntered=true;
        tagReadWake.wait_for(lock,std::chrono::seconds{5},[]{return !holdTagRead.load();});
      }
    }
  }
  return __real_read(descriptor, bytes, size);
}

class ManualTagBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void normalProfileRulesPersistThroughActualWorkerStoreAndSharedMembership(){
    test::OwnedEngineFixture fixture(false);
    const auto tagPath=fixture.profile+QStringLiteral("/tag.config");
    libtorrent::entry definitions(libtorrent::entry::dictionary_type{});
    const auto rule=[&](int id,const char* expression,const char* options,bool member){
      auto& record=definitions["3"][std::to_string(id)];record["n"]="Rule "+std::to_string(id);
      record["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry(options)};
      record["o"]=libtorrent::entry::list_type{};
      if(member){record["o"].list().emplace_back(base32(*fixture.hash));}
    };
    rule(1,"size>0","",false);rule(2,"size<0","",true);
    rule(3,"down_speed>0","",true);rule(4,"size>0","am=2;",false);
    rule(5,"size<0","am=1;",true);rule(6,"true","am=3;",false);
    rule(7,"unknownFunction()","",true);rule(8,"true","",false);
    definitions["3"]["8"]["c"]["unimplemented.action"]=1;
    rule(9,"true","",false);definitions["3"]["9"]["c"]["pp.enabled.constraint"]=0;
    definitions["3"]["c"]["opaque"]=std::string("\0\xffT",3);
    definitions["unknown-root"]=std::string("\0\xffR",3);
    test::write(tagPath,QByteArray::fromStdString(test::encoded(definitions)));
    auto inventory=core::Document::parse(test::read(fixture.profile+"/downloads.config").toStdString());QVERIFY(inventory);
    const auto* first=&inventory->find("downloads")->children[0];
    auto row=core::Document::parse(inventory->originalBytes().substr(first->begin,first->end-first->begin));QVERIFY(row);
    QVERIFY(row->setInteger("state",70));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));
    const auto originalInventory=QByteArray::fromStdString(inventory->serialize());test::write(fixture.profile+"/downloads.config",originalInventory);
    const auto originalState=test::read(fixture.stateFile),originalPayload=test::read(fixture.payload+"/sample.bin");
    libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;
    settings["TCP.Listen.Port"]=6881;settings["UDP.Listen.Port"]=6881;settings["IPV6 Enable Support"]=0;
    for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable",
        "PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange",
        "Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}
    test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),statistics(&bridge,&app::SessionBridge::manualTagStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    const auto count=[&](int id){
      const auto packet=latest(statistics);if(!packet||!packet->membership){return std::size_t{999999};}
      const auto uid=(std::int64_t{3}<<32)|id;
      for(const auto& value:packet->membership->rows()){if(value.uid==uid){return value.resolvedCount;}}
      return std::size_t{999999};
    };
    QTRY_COMPARE_WITH_TIMEOUT(count(1),std::size_t{1},10000);
    QTRY_COMPARE_WITH_TIMEOUT(count(2),std::size_t{0},10000);
    QCOMPARE(count(3),std::size_t{1});QCOMPARE(count(4),std::size_t{0});QCOMPARE(count(5),std::size_t{1});
    QCOMPARE(count(6),std::size_t{0});QCOMPARE(count(7),std::size_t{1});QCOMPARE(count(8),std::size_t{0});QCOMPARE(count(9),std::size_t{0});
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
    QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    const auto saved=core::readSavedDownloadTags(test::read(tagPath).toStdString());QVERIFY(saved);
    const auto before=core::Document::parse(test::encoded(definitions));QVERIFY(before);
    const auto raw=[](const core::Document& source,const core::Node* value){return source.originalBytes().substr(value->begin,value->end-value->begin);};
    QCOMPARE(raw(*saved->source,saved->source->find("unknown-root")),raw(*before,before->find("unknown-root")));
    QCOMPARE(raw(*saved->source,saved->source->find("3")->find("c")),raw(*before,before->find("3")->find("c")));
    for(const auto& value:saved->tags){
      const auto* current=saved->source->find("3")->find(value.idKey);
      const auto* prior=before->find("3")->find(value.idKey);
      QCOMPARE(raw(*saved->source,current->find("c")),raw(*before,prior->find("c")));
    }
    QCOMPARE(test::read(fixture.stateFile),originalState);QCOMPARE(test::read(fixture.profile+"/downloads.config"),originalInventory);
    QCOMPARE(test::read(fixture.payload+"/sample.bin"),originalPayload);
    auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
  }
  void actualRuleWorkerRefusesStaleForeignCancelledAndClosedTurns(){
    test::OwnedEngineFixture fixture(false),foreign(false);
    libtorrent::entry definitions(libtorrent::entry::dictionary_type{});definitions["3"]["1"]["n"]="Rule";
    definitions["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("true")};
    const auto original=QByteArray::fromStdString(test::encoded(definitions));
    test::write(fixture.profile+"/tag.config",original);test::write(foreign.profile+"/tag.config",original);
    // The test supplies an external worker. All Store/source operations below
    // run there; no GUI callback, fabricated permit or provider is involved.
    auto actual=std::async(std::launch::async,[&]{
      auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual rule lease");
      auto other=storage::ProfileLease::acquire(foreign.native());test::require(bool(other),"Actual foreign lease");
      auto store=storage::ManualTagStore::open(*lease),foreignStore=storage::ManualTagStore::open(*other);
      test::require(bool(store)&&bool(foreignStore),"Actual rule stores");
      auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual rule preimage");
      auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;
      ui::LibraryItem row;row.hash=QString::fromStdString(fixture.hash->hex());row.name="sample.bin";row.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(row);
      app::ConstraintWorkerFrame frame;frame.session=71;frame.sequence=1;frame.library=library;frame.sourceRevision=lease->sourceRevision();
      const auto worker=[&]{auto result=app::TagConstraintWorker::activate(*lease,71);test::require(bool(result),"Actual rule worker");return std::move(*result);};
      {
        auto value=worker();std::stop_source cancelled;cancelled.request_stop();
        auto result=value->step(**store,*snapshot,frame,cancelled.get_token());test::require(!result&&result.error().code==storage::ErrorCode::Cancelled,"Cancelled rule turn published");
      }
      {
        auto value=worker();auto stale=frame;stale.session=72;auto result=value->step(**store,*snapshot,stale);
        test::require(!result&&result.error().code==storage::ErrorCode::Conflict,"Foreign session accepted");
        stale=frame;++stale.sourceRevision;result=value->step(**store,*snapshot,stale);
        test::require(bool(result)&&!result->changed&&result->kept&&result->kept->kind==core::tags::constraints::ProblemKind::Unavailable,"Stale source epoch authorized an edit");
      }
      {
        auto value=worker();auto stale=frame;
        auto rows=std::make_shared<ui::LibraryPresentation>(*library);rows->items[0].providerAdmission=91;rows->items[0].providerGeneration=frame.session;stale.library=rows;
        engine::ManagedDownloadStatus status;status.hash=*fixture.hash;status.admission=92;status.retained=true;status.bytes=test::OwnedEngineFixture::PayloadBytes;
        stale.managed.push_back(status);auto result=value->step(**store,*snapshot,stale);
        test::require(bool(result)&&!result->changed&&result->kept&&result->kept->kind==core::tags::constraints::ProblemKind::Unavailable,"Stale provider admission did not remain unavailable");
      }
      // A literal true must still acquire a complete current fact capture.
      // These negative DTO inputs cannot invent source authority or a provider.
      for(unsigned kind=0;kind<5;++kind){
        auto value=worker();auto stale=frame;
        auto rows=std::make_shared<ui::LibraryPresentation>(*library);
        rows->items[0].providerAdmission=91;rows->items[0].providerGeneration=frame.session;stale.library=rows;
        engine::ManagedDownloadStatus status;status.hash=*fixture.hash;status.admission=91;status.retained=true;status.bytes=test::OwnedEngineFixture::PayloadBytes;
        if(kind==0){/* advertised binding, no actual observation */}
        if(kind==1){rows->items[0].providerGeneration=frame.session+1;stale.managed.push_back(status);}
        if(kind==2){stale.managed.push_back(status);stale.managed.push_back(status);}
        if(kind==3){status.retained=false;stale.managed.push_back(status);}
        if(kind==4){rows->items[0].providerAdmission=rows->items[0].providerGeneration=0;stale.managed.push_back(status);}
        auto result=value->step(**store,*snapshot,stale);
        test::require(bool(result)&&!result->changed&&result->kept&&result->kept->kind==core::tags::constraints::ProblemKind::Unavailable,"Literal true bypassed missing/stale provider facts");
        test::require(test::read(fixture.profile+"/tag.config")==original,"Refused literal true provider turn changed membership");
      }
      {
        auto value=worker();auto foreignSnapshot=(*foreignStore)->snapshot();test::require(bool(foreignSnapshot),"Foreign preimage");
        auto result=value->step(**foreignStore,*foreignSnapshot,frame);test::require(!result&&result.error().code==storage::ErrorCode::Conflict,"Equal bytes crossed OFD profile ownership");
      }
      {
        auto value=worker();auto fresh=(*store)->reload();test::require(bool(fresh),"Actual newer snapshot");
        test::require(snapshot->sameSource(*fresh),"Unchanged reload changed captured source identity");
        auto result=value->step(**store,*snapshot,frame);test::require(!result&&result.error().code==storage::ErrorCode::Conflict,"Old tag generation accepted");snapshot=std::move(fresh);
      }
      {
        const auto before=*snapshot;const auto replacement=fixture.profile+"/tag.config.rule-test";
        test::write(replacement,original);std::filesystem::rename(replacement.toStdString(),(fixture.profile+"/tag.config").toStdString());
        auto fresh=(*store)->reload();test::require(bool(fresh),"Actual same-byte identity reload");
        test::require(before.rawBytes()==fresh->rawBytes()&&!before.sameSource(*fresh),"Same-byte atomic replacement hid a changed source identity");snapshot=std::move(fresh);
      }
      {
        auto value=worker();value->close();auto result=value->step(**store,*snapshot,frame);
        test::require(!result&&result.error().code==storage::ErrorCode::Conflict,"Closed rule worker restarted");
      }
      test::require(test::read(fixture.profile+"/tag.config")==original&&test::read(foreign.profile+"/tag.config")==original,"Refused rule turn changed a tag file");
      {
        auto value=worker();auto changed=definitions;
        changed["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("false")};
        const auto bytes=QByteArray::fromStdString(test::encoded(changed));test::write(fixture.profile+"/tag.config",bytes);
        frame.sourceRevision=lease->sourceRevision();auto refused=value->step(**store,*snapshot,frame);
        test::require(!refused&&refused.error().code==storage::ErrorCode::Conflict,"Stale rule source overwrote user change");
        test::require(test::read(fixture.profile+"/tag.config")==bytes,"Refused rule replay changed source");
        auto fresh=(*store)->reload();test::require(bool(fresh),"Explicit source reload");
        test::require(!snapshot->sameSource(*fresh),"External source mutation hid behind reload generation");snapshot=std::move(fresh);
        auto replacement=worker();frame.sourceRevision=lease->sourceRevision();auto kept=replacement->step(**store,*snapshot,frame);
        test::require(bool(kept)&&!kept->changed,"Fresh false rule retained old true decision");
        test::write(fixture.profile+"/tag.config",original);fresh=(*store)->reload();test::require(bool(fresh),"Restore test-owned rule source");snapshot=std::move(fresh);
      }
      {
        auto value=worker();frame.sourceRevision=lease->sourceRevision();auto result=value->step(**store,*snapshot,frame);
        test::require(bool(result)&&result->changed&&result->hash==fixture.hash,"Actual valid rule did not commit");
      }
      test::require(bool((*store)->close())&&bool((*foreignStore)->close()),"Rule store close");
      return true;
    });
    QVERIFY(actual.get());auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
    const auto saved=core::readSavedDownloadTags(test::read(fixture.profile+"/tag.config").toStdString());QVERIFY(saved);
    QCOMPARE(saved->tags.front().uniqueHashes.size(),std::size_t{1});QVERIFY(saved->tags.front().uniqueHashes.front()==*fixture.hash);
  }
  void ruleCacheEvictionEventuallyCommitsBeyond128WithoutExtraTimers(){
    test::OwnedEngineFixture fixture(false);libtorrent::entry definitions(libtorrent::entry::dictionary_type{});
    for(int id=1000;id<1130;++id){auto& row=definitions["3"][std::to_string(id)];row["n"]="Cached rule";
      row["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("true")};
      row["o"]=libtorrent::entry::list_type{};if(id!=1129){row["o"].list().emplace_back(base32(*fixture.hash));}
    }
    test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(definitions)));
    auto actual=std::async(std::launch::async,[&]{
      auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Cache actual lease");
      auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Cache actual Store");
      auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Cache actual preimage");
      auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"Cache actual worker");
      auto rows=std::make_shared<ui::LibraryPresentation>();rows->revision=9;
      ui::LibraryItem row;row.hash=QString::fromStdString(fixture.hash->hex());row.bytes=test::OwnedEngineFixture::PayloadBytes;rows->items.push_back(row);
      app::ConstraintWorkerFrame frame;frame.session=88;frame.library=rows;unsigned evaluated=0,committed=0;
      for(unsigned turn=1;turn<=5;++turn){frame.sequence=turn;frame.sourceRevision=lease->sourceRevision();
        auto result=(*worker)->step(**store,*snapshot,frame);test::require(bool(result),"Cache turn refused");
        test::require(result->evaluated<=32,"Rule turn exceeded finite work");evaluated+=result->evaluated;
        if(result->changed){++committed;test::require(result->tag==1129,"Earlier already-present rule was rewritten");snapshot=result->snapshot;}
      }
      test::require(evaluated==130&&committed==1,"128-entry cache became a rule/lifetime cap");
      (*worker)->close();test::require(bool((*store)->close()),"Cache store close");return true;
    });
    QVERIFY(actual.get());auto saved=core::readSavedDownloadTags(test::read(fixture.profile+"/tag.config").toStdString());QVERIFY(saved);
    const auto final=std::find_if(saved->tags.begin(),saved->tags.end(),[](const auto& value){return value.id==1129;});
    QVERIFY(final!=saved->tags.end()&&final->uniqueHashes.size()==1&&final->uniqueHashes.front()==*fixture.hash);
  }
  void offlinePreviewNeverActivatesAutomaticRules(){
    test::OwnedEngineFixture fixture(false);libtorrent::entry definitions(libtorrent::entry::dictionary_type{});
    definitions["3"]["1"]["n"]="Must remain empty";
    definitions["3"]["1"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("true")};
    const auto original=QByteArray::fromStdString(test::encoded(definitions));const auto path=fixture.profile+"/tag.config";test::write(path,original);Watch watch(path);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),statistics(&bridge,&app::SessionBridge::manualTagStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QTimer* idle=nullptr;for(auto* timer:bridge.findChildren<QTimer*>()){if(timer->interval()==1000){idle=timer;}}
    QVERIFY(idle);QSignalSpy turns(idle,&QTimer::timeout);QTRY_VERIFY_WITH_TIMEOUT(turns.count()>=2,4000);
    QCOMPARE(tagReads.load(),0U);QCOMPARE(statistics.count(),0);QCOMPARE(test::read(path),original);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
  void offlineReadsAreLazyRefreshesAreExplicitAndFailuresInvalidateReady() {
    test::OwnedEngineFixture fixture(false);
    const auto path = fixture.profile + QStringLiteral("/tag.config");
    const auto raw = tags(*fixture.hash); test::write(path, raw); Watch watch(path);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QVERIFY(bridge.open(fixture.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty() && !sessions.empty(), 10000);
    QCOMPARE(tagReads.load(), 0U); QCOMPARE(packets.count(), 0);
    const auto session = sessions.last().at(0).toULongLong();
    QVERIFY(!bridge.manualTagStatistics(0)); QVERIFY(!bridge.manualTagStatistics(session + 1));
    bool busyDuringPacket = false, reentryRejected = false;
    connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, &bridge, [&](auto) {
      if (bridge.busy()) { busyDuringPacket = true; reentryRejected = !bridge.manualTagStatistics(session); }
    });
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    const auto first = latest(packets); QCOMPARE(first->readState, ui::ManualTagReadState::Ready);
    QVERIFY(first->membership); QVERIFY(tagReads.load() > 0); QVERIFY(busyDuringPacket && reentryRejected);
    QCOMPARE(first->epoch, quint64{0}); QCOMPARE(first->traffic.state, core::ManualTagTrafficState::Unavailable);
    auto rates = core::projectManualTagRates(*first->membership, first->traffic); QVERIFY(rates);
    QVERIFY(!rates->at(0).downloadRate); QVERIFY(!rates->at(1).downloadRate);
    const auto oldReads = tagReads.load();
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() >= 2, 10000);
    QTRY_VERIFY(latest(packets)->libraryRevision > first->libraryRevision);
    QCOMPARE(tagReads.load(), oldReads); // Library join uses cached bytes, no tag read.
    QVERIFY(latest(packets)->membership != first->membership);
    QCOMPARE(latest(packets)->tagRevision, first->tagRevision);
    test::write(path, QByteArray("malformed"));
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->tagRevision > first->tagRevision, 10000);
    const auto failed = latest(packets); QCOMPARE(failed->readState, ui::ManualTagReadState::Failed);
    QVERIFY(!failed->membership); QVERIFY(!failed->failure.isEmpty());
    QCOMPARE(failed->traffic.state, core::ManualTagTrafficState::Unavailable);
    QVERIFY(failed->sequence > first->sequence);
    QCOMPARE(test::read(path), QByteArray("malformed"));
    test::write(path, raw); const auto beforeRetry = failed->tagRevision;
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->tagRevision > beforeRetry, 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Ready); QCOMPARE(test::read(path), raw);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(path + QStringLiteral(".saving")));
    QVERIFY(fixture.intact(false));
  }
  void actualSingleAndMultifileRatesAndJoinedClose_data() {
    QTest::addColumn<bool>("multifile"); QTest::newRow("single") << false; QTest::newRow("multifile") << true;
  }
  void actualSingleAndMultifileRatesAndJoinedClose() {
    QFETCH(bool, multifile); test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.profile + QStringLiteral("/tag.config");
    const auto raw = tags(*fixture.hash); test::write(path, raw); const auto before = tagStamp(path); Watch watch(path);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QSignalSpy owner(&bridge, &app::SessionBridge::nativeSessionReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty() && !sessions.empty(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(native(owner) && native(owner)->state == ui::NativeSessionState::Idle && native(owner)->sample, 10000);
    QCOMPARE(tagReads.load(), 0U); QCOMPARE(packets.count(), 0);
    const auto session = sessions.last().at(0).toULongLong();
    const auto capturePath = qEnvironmentVariable("BIGLYBTC_MANUAL_TAG_CAPTURE_DIRECTORY");
    std::unique_ptr<CaptureDirectory> captureDirectory;
    std::unique_ptr<ui::ManualTagStatisticsView> captureView;
    bool captured = false; QString captureFailure;
    if (!capturePath.isEmpty()) {
      captureDirectory = std::make_unique<CaptureDirectory>(capturePath);
      captureView = std::make_unique<ui::ManualTagStatisticsView>();
      QVERIFY(captureView->beginSession(session)); captureView->resize(1180, 650); captureView->show();
      // No readRequested connection: the existing explicit read remains the only
      // tag IO. The module renders actual packets from the real Bridge signal.
      connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, captureView.get(),
          &ui::ManualTagStatisticsView::setPresentation);
      connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, captureView.get(), [&](auto packet) {
        if (captured || !captureFailure.isEmpty() || !bridge.busy()) { return; }
        try {
          if (!positiveTagRendered(*captureView, packet)) { return; }
          captured = captureDirectory->save(*captureView, packet,
              multifile ? "actual-manual-tags-multi.png" : "actual-manual-tags-single.png");
          if (!captured) { captureFailure = QStringLiteral("Cannot save owned real Manual Tag capture"); }
        } catch (...) { captureFailure = QStringLiteral("Cannot render owned real Manual Tag capture"); }
      });
    }
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets) && latest(packets)->traffic.state == core::ManualTagTrafficState::Idle, 10000);
    const auto idle = latest(packets); QVERIFY(idle->epoch); QVERIFY(idle->traffic.completeOwnedCoverage);
    auto zero = core::projectManualTagRates(*idle->membership, idle->traffic); QVERIFY(zero);
    QCOMPARE(zero->at(0).downloadRate, std::optional<std::int64_t>{0});
    const auto readsAfterRequest = tagReads.load(); QVERIFY(readsAfterRequest > 0);
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(sawPositive(packets), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() >= 2, 30000);
    bool checkedPositive = false;
    quint64 previous = 0;
    for (const auto& row : packets) {
      const auto p = qvariant_cast<ui::ManualTagStatisticsPresentation>(row.at(0));
      QCOMPARE(p->session, session); QVERIFY(p->sequence > previous); previous = p->sequence;
      if (p->readState == ui::ManualTagReadState::Ready && p->libraryRevision == idle->libraryRevision
          && p->tagRevision == idle->tagRevision) { QVERIFY(p->membership == idle->membership); }
      if (!activePositive(p)) { continue; }
      QVERIFY(p->traffic.target->hash == *fixture.hash); QVERIFY(p->traffic.completeOwnedCoverage);
      auto rates = core::projectManualTagRates(*p->membership, p->traffic); QVERIFY(rates);
      QCOMPARE(rates->at(0).state, core::ManualTagRateState::ObservedActive);
      QCOMPARE(rates->at(0).downloadRate, std::optional{p->traffic.target->downloadRate});
      QCOMPARE(rates->at(1).state, core::ManualTagRateState::ObservedOtherTagZero);
      QCOMPARE(rates->at(1).downloadRate, std::optional<std::int64_t>{0});
      QCOMPARE(rates->at(2).state, core::ManualTagRateState::UnresolvedMembership);
      QVERIFY(!rates->at(2).downloadRate); checkedPositive = true;
    }
    QVERIFY(checkedPositive); QCOMPARE(tagReads.load(), readsAfterRequest);
    if (captureDirectory) { QVERIFY2(captured, qPrintable(captureFailure.isEmpty()
        ? QStringLiteral("No actual positive Manual Tag frame was captured") : captureFailure)); }
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(latest(packets) && !latest(packets)->traffic.current);
    QVERIFY(!bridge.manualTagStatistics(session)); // Closed worker cannot reread.
    QCOMPARE(tagReads.load(), readsAfterRequest); QCOMPARE(test::read(path), raw); QVERIFY(sameTag(before, path));
    QVERIFY(fixture.accountingIntact());
    QVERIFY(idle->traffic.current); QCOMPARE(idle->traffic.state, core::ManualTagTrafficState::Idle);
  }
  void cancellationRetiresActualHandleWithoutTagWrites() {
    test::OwnedEngineFixture fixture(false, true); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.profile + QStringLiteral("/tag.config"); const auto raw = tags(*fixture.hash); test::write(path, raw);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy packets(&bridge, &app::SessionBridge::manualTagStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !sessions.empty(), 10000);
    const auto session = sessions.last().at(0).toULongLong();
    QVERIFY(bridge.manualTagStatistics(session)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    bool cancelled = false;
    connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, &bridge, [&](auto packet) {
      if (!cancelled && bridge.busy() && activePositive(packet)) { cancelled = true; bridge.cancel(); }
    });
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(cancelled && !bridge.busy(), 30000);
    QCOMPARE(test::read(path), raw); QVERIFY(fixture.accountingIntact(false));
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(latest(packets) && !latest(packets)->traffic.current);
  }
  void closeThenNewProfilePublishesANewSessionAndRejectsOldRequests() {
    test::OwnedEngineFixture first(false), second(false); app::SessionBridge bridge; ui::LibraryWindow window;
    QTRY_VERIFY_WITH_TIMEOUT(second.ready(), 30000);
    auto inventory = test::read(second.profile + QStringLiteral("/downloads.config"));
    QCOMPARE(inventory.count("10:downloadedi0e"), 1);
    inventory.replace("10:downloadedi0e", "10:downloadedi42e");
    test::write(second.profile + QStringLiteral("/downloads.config"), inventory);
    QVERIFY(*first.hash == *second.hash); // Same identity; generation must carry profile fencing.
    connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
    connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
    connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy owner(&bridge, &app::SessionBridge::nativeSessionReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(first.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && sessions.count() == 1, 10000);
    const auto oldSession = sessions.first().at(0).toULongLong();
    QVERIFY(window.model().targetAt(0)); const auto oldTarget = *window.model().targetAt(0);
    QVERIFY(bridge.manualTagStatistics(oldSession)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Missing);
    bool attemptedEarly = false, openedEarly = false, reopenedInAck = false, closedCommandsRejected = false;
    connect(&bridge, &app::SessionBridge::busyChanged, &bridge, [&](bool busy) {
      if (!busy && !attemptedEarly) { attemptedEarly = true; openedEarly = bridge.open(second.native(), second.port()); }
    });
    connect(&bridge, &app::SessionBridge::sessionClosed, &bridge, [&](quint64 session, bool, bool) {
      if (session == oldSession) {
        closedCommandsRejected = !bridge.refresh() && !bridge.savedServices(1)
            && !bridge.details(oldTarget) && !bridge.continueDownload(oldTarget);
        reopenedInAck = bridge.open(second.native(), second.port());
      }
    });
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(attemptedEarly); QVERIFY(!openedEarly); QVERIFY(closedCommandsRejected); QVERIFY(reopenedInAck);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && sessions.count() == 2, 10000);
    const auto nextSession = sessions.last().at(0).toULongLong(); QVERIFY(nextSession > oldSession);
    QTRY_VERIFY_WITH_TIMEOUT(native(owner) && native(owner)->session == nextSession
        && native(owner)->state == ui::NativeSessionState::Idle && native(owner)->sample, 10000);
    // The existing idle monitor was stopped for close and must resume for the new owner.
    QVERIFY(native(owner)->epoch);
    QVERIFY(window.model().targetAt(0)); const auto nextTarget = *window.model().targetAt(0);
    QCOMPARE(nextTarget.hash, oldTarget.hash); QVERIFY(nextTarget.generation > oldTarget.generation);
    QCOMPARE(window.model().data(window.model().index(0, 6), Qt::UserRole).toLongLong(), qlonglong{42});
    QVERIFY(bridge.continueDownload(oldTarget));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !messages.empty(), 10000);
    QCOMPARE(operations.count(), 0); QVERIFY(messages.last().at(0).toString().contains(QStringLiteral("stale")));
    QVERIFY(!bridge.manualTagStatistics(oldSession)); QVERIFY(bridge.manualTagStatistics(nextSession));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->session == nextSession, 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Missing);
    QCOMPARE(latest(packets)->epoch, native(owner)->epoch); QVERIFY(latest(packets)->tagRevision);
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 2, 15000);
  }
  void directPacketReceiverMayDeleteBridgeWithoutTrailingBusySignal() {
    test::OwnedEngineFixture fixture(false); const auto raw = tags(*fixture.hash);
    test::write(fixture.profile + QStringLiteral("/tag.config"), raw);
    QObject cleanup; auto* bridge = new app::SessionBridge(&cleanup); QPointer<app::SessionBridge> alive(bridge);
    QSignalSpy sessions(bridge, &app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(bridge->open(fixture.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy() && !sessions.empty(), 10000);
    const auto session = sessions.first().at(0).toULongLong();
    bool deleted = false; int trailingIdle = 0;
    connect(bridge, &app::SessionBridge::busyChanged, &cleanup, [&](bool busy) { if (!busy) { ++trailingIdle; } });
    connect(bridge, &app::SessionBridge::manualTagStatisticsReady, &cleanup, [&](auto packet) {
      if (packet->session == session) { QVERIFY(bridge->busy()); delete bridge; deleted = true; }
    });
    QVERIFY(bridge->manualTagStatistics(session)); QTRY_VERIFY(deleted && alive.isNull()); QCOMPARE(trailingIdle, 0);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
  }
  void originalSavedAutomationClassificationIsDirectionalWithoutRuleEvaluation() {
    const auto string=[](std::string_view value){return std::to_string(value.size())+":"+std::string(value);};
    const auto dictionary=[&](std::initializer_list<std::pair<std::string,std::string>> fields){std::string out="d";for(const auto& [name,value]:fields){out+=string(name)+value;}return out+"e";};
    const auto list=[&](std::initializer_list<std::string_view> values){std::string out="l";for(const auto value:values){out+=string(value);}return out+"e";};
    struct Case{std::string config;bool add,remove,other,malformed;};
    const std::vector<Case> cases{
      {"de",true,true,false,false},
      {dictionary({{"pp.trackers",list({})},{"pp.untagged","i0e"},{"pp.constraint",list({})}}),true,true,false,false},
      {dictionary({{"pp.trackers",list({"tracker"})},{"pp.constraint",list({"true","am=3;"})}}),false,false,false,false},
      {dictionary({{"pp.untagged","i1e"}}),false,false,false,false},
      {dictionary({{"pp.untagged","i2e"}}),true,true,false,false},
      {dictionary({{"pp.tracker_templates",list({"inert template"})}}),true,true,false,false},
      {dictionary({{"pp.constraint",list({"true"})},{"pp.enabled.constraint","i0e"}}),true,true,false,false},
      {dictionary({{"pp.constraint",list({" \t\r\n"})}}),true,true,false,false},
      {dictionary({{"pp.constraint",list({"never evaluated","am=1;"})}}),false,true,false,false},
      {dictionary({{"pp.constraint",list({"never evaluated","am=2;"})}}),true,false,false,false},
      {dictionary({{"pp.constraint",list({"never evaluated","am=3;"})}}),true,true,true,false},
      {dictionary({{"pp.constraint",list({"never evaluated","am=3;am=2;am=1;"})}}),false,true,false,false},
      {dictionary({{"pp.trackers","i1e"}}),false,false,false,true},
      {dictionary({{"pp.constraint",list({"true"})},{"pp.enabled.constraint",string("wrong")}}),false,false,false,true},
      {"i1e",false,false,false,true}
    };
    for(const auto& value:cases){
      const auto raw=dictionary({{"c",value.config},{"unknown",string("preserved")}});auto parsed=core::Document::parse(raw);QVERIFY(parsed);
      const auto classified=app::classifyManualTagMembership(parsed->root());
      QCOMPARE(classified.addAllowed(),value.add);QCOMPARE(classified.removeAllowed(),value.remove);QCOMPARE(classified.other,value.other);QCOMPARE(classified.malformed,value.malformed);QCOMPARE(parsed->serialize(),raw);
    }
    // Every policy input is freshly parsed. The original map last-wins model
    // is ambiguous here, so the shared strict parser rejects every duplicate.
    const auto duplicateConfig=dictionary({{"c","de"},{"c","de"}});auto duplicate=core::Document::parse(duplicateConfig);QVERIFY(!duplicate&&duplicate.error().code=="duplicate_key");
    for(const auto* key:{"pp.trackers","pp.untagged","pp.constraint","pp.enabled.constraint"}){
      const auto raw=dictionary({{"c",dictionary({{key,"i0e"},{key,"i1e"}})}});duplicate=core::Document::parse(raw);QVERIFY(!duplicate&&duplicate.error().code=="duplicate_key");
    }
  }
  void savedTagCommandsCreateAssignRenameRefreshAndJoinedClose() {
    test::OwnedEngineFixture fixture(false);app::SessionBridge bridge;
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy commands(&bridge,&app::SessionBridge::manualTagCommandFinished),statistics(&bridge,&app::SessionBridge::manualTagStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&!rows.empty(),10000);
    const auto session=sessions.last().at(0).toULongLong();const auto result=[&]{return qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));};
    ui::ManualTagCommand request;request.session=session;request.request=1;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==1);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QCOMPARE(result()->readState,ui::ManualTagReadState::Missing);QVERIFY(result()->writable&&result()->tags&&result()->tags->source->originalBytes()=="de");
    request={};request.session=session;request.request=2;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Create;request.name=QStringLiteral("Created");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==2);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(result()->createdId,std::optional<qint32>{1});QVERIFY(result()->changed&&result()->writable);
    request.request=3;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::AddMember;request.tagId=1;request.member=target(rows);
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==3);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(result()->tags->tags.front().uniqueHashes.size(),std::size_t{1});QVERIFY(result()->tags->tags.front().uniqueHashes.front()==*fixture.hash);
    QVERIFY(latest(statistics)&&latest(statistics)->membership);QCOMPARE(latest(statistics)->tagRevision,result()->generation);
    request.request=4;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Rename;request.name=QStringLiteral("Renamed");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==4);
    QCOMPARE(result()->tags->tags.front().rawName,std::optional<std::string>{"Renamed"});const auto oldGeneration=result()->generation;
    QVERIFY(bridge.manualTagStatistics(session));QTRY_VERIFY(!bridge.busy()&&latest(statistics)->tagRevision>oldGeneration);
    const auto path=fixture.profile+QStringLiteral("/tag.config");const auto before=test::read(path);
    request.request=5;request.generation=oldGeneration;request.name=QStringLiteral("Must not overwrite");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==5);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(result()->failure.contains(QStringLiteral("stale")));QCOMPARE(test::read(path),before);
    request.request=6;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==6);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);
    request.request=7;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Create;request.name=QStringLiteral("Same actual session");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==7);QCOMPARE(result()->createdId,std::optional<qint32>{2});
    const auto heldPresentation=result();QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
    QVERIFY(closed.last().at(1).toBool()&&closed.last().at(2).toBool());QVERIFY(!bridge.manualTagCommand(request));
    auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);QVERIFY(heldPresentation->tags&&heldPresentation->tags->tags.front().rawName==std::optional<std::string>{"Renamed"});
  }
  void savedTagNoopAndStaleLibrarySelectionNeverPublish() {
    test::OwnedEngineFixture fixture(false);const auto path=fixture.profile+QStringLiteral("/tag.config");const auto raw=tags(*fixture.hash);test::write(path,raw);
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),commands(&bridge,&app::SessionBridge::manualTagCommandFinished);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!sessions.empty()&&!rows.empty());
    ui::ManualTagCommand request;request.session=sessions.last().at(0).toULongLong();request.request=1;
    const auto result=[&]{return qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));};
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==1);const auto generation=result()->generation;const auto before=tagStamp(path);const auto owning=result()->tags;
    request.request=2;request.generation=generation;request.kind=ui::ManualTagCommandKind::AddMember;request.tagId=1;request.member=target(rows);
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==2);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Unchanged);QVERIFY(!result()->changed&&result()->tags==owning);QCOMPARE(result()->generation,generation);QVERIFY(sameTag(before,path));QCOMPARE(test::read(path),raw);
    ++request.request;++request.member.generation;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==3);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(result()->failure.contains(QStringLiteral("stale")));QVERIFY(sameTag(before,path));
    ++request.request;request.member=target(rows);request.member.hash=QString(40,QLatin1Char('F'));
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==4);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(path),raw);
    QVERIFY(!QFileInfo::exists(path+QStringLiteral(".bak"))&&!QFileInfo::exists(path+QStringLiteral(".saving")));
    auto automated=raw;QCOMPARE(automated.count("1:n6:Actual"),1);automated.replace("1:n6:Actual","1:cd13:pp.constraintl4:trueee1:n6:Actual");test::write(path,automated);
    request.request=5;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==5);
    request.request=6;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::AddMember;request.member=target(rows);
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==6);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(result()->failure.contains(QStringLiteral("Automatic")));QCOMPARE(test::read(path),automated);QVERIFY(!QFileInfo::exists(path+QStringLiteral(".saving")));
    auto directional=raw;directional.replace("1:n6:Actual","1:cd13:pp.constraintl4:true5:am=1;ee1:n6:Actual");test::write(path,directional);
    request.request=7;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==7);
    request.request=8;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::RemoveMember;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==8);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QVERIFY(result()->tags->tags.front().uniqueHashes.empty());
    const auto removed=test::read(path);request.request=9;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::AddMember;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==9);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(path),removed);
    auto disabled=removed;QCOMPARE(disabled.count("13:pp.constraint"),1);disabled.replace("13:pp.constraint","21:pp.enabled.constrainti0e13:pp.constraint");test::write(path,disabled);
    request.request=10;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==10);
    request.request=11;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::AddMember;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==11);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QVERIFY(result()->tags->tags.front().uniqueHashes.front()==*fixture.hash);
  }
  void savedTagCancelledActualReadCreatesNoPublicationAndCanRetry() {
    test::OwnedEngineFixture fixture(false);const auto path=fixture.profile+QStringLiteral("/tag.config");const auto raw=tags(*fixture.hash);test::write(path,raw);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),commands(&bridge,&app::SessionBridge::manualTagCommandFinished);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!sessions.empty());
    ui::ManualTagCommand request;request.session=sessions.last().at(0).toULongLong();request.request=1;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==1);
    const auto first=qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));Watch watch(path);
    const auto release=qScopeGuard([]{holdTagRead=false;tagReadWake.notify_all();});
    heldTagReadEntered=false;holdTagRead=true;
    request.request=2;request.generation=first->generation;request.kind=ui::ManualTagCommandKind::Create;request.name=QStringLiteral("Cancelled");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY_WITH_TIMEOUT(heldTagReadEntered.load(),3000);bridge.cancel();holdTagRead=false;tagReadWake.notify_all();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&commands.count()==2,10000);
    const auto cancelled=qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));QCOMPARE(cancelled->status,ui::ManualTagCommandStatus::Cancelled);
    QCOMPARE(test::read(path),raw);QVERIFY(!QFileInfo::exists(path+QStringLiteral(".saving")));QVERIFY(!QFileInfo::exists(path+QStringLiteral(".bak")));
    request.request=3;request.generation=first->generation;request.name=QStringLiteral("Actual retry");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==3);QCOMPARE(qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0))->status,ui::ManualTagCommandStatus::Saved);
  }
  void savedTagRecoveryIsReadOnlyAndChangedCandidatesRequireReload() {
    test::OwnedEngineFixture fixture(false);const auto path=fixture.profile+QStringLiteral("/tag.config");const auto raw=tags(*fixture.hash);test::write(path,raw);test::write(path+QStringLiteral(".saving"),raw);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),commands(&bridge,&app::SessionBridge::manualTagCommandFinished);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!sessions.empty());
    ui::ManualTagCommand request;request.session=sessions.last().at(0).toULongLong();request.request=1;
    const auto result=[&]{return qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));};
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==1);QVERIFY(!result()->writable&&result()->tags);
    request.request=2;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Rename;request.tagId=1;request.name=QStringLiteral("Denied repair");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==2);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QCOMPARE(test::read(path),raw);QCOMPARE(test::read(path+QStringLiteral(".saving")),raw);
    QVERIFY(QFile::remove(path+QStringLiteral(".saving")));request.request=3;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==3);QVERIFY(result()->writable);
    test::write(path+QStringLiteral(".replacement"),raw);QVERIFY(QFile::remove(path));QVERIFY(QFile::rename(path+QStringLiteral(".replacement"),path));
    request.request=4;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Rename;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==4);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(!result()->writable);QCOMPARE(test::read(path),raw);QVERIFY(!QFileInfo::exists(path+QStringLiteral(".saving")));
    request.request=5;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==5);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QVERIFY(result()->writable);
  }
  void savedTagExplicitReloadReopensUncertainStoreWithoutReplayingEdit() {
    QVERIFY(::geteuid()!=0);test::OwnedEngineFixture fixture(false);app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),commands(&bridge,&app::SessionBridge::manualTagCommandFinished);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!sessions.empty());
    ui::ManualTagCommand request;request.session=sessions.last().at(0).toULongLong();request.request=1;
    const auto result=[&]{return qvariant_cast<ui::ManualTagCommandPresentation>(commands.last().at(0));};
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==1);
    request.request=2;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Create;request.name=QStringLiteral("Committed first");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==2);QCOMPARE(result()->createdId,std::optional<qint32>{1});
    const auto path=fixture.profile+QStringLiteral("/tag.config");const auto before=test::read(path);const auto generation=result()->generation;
    const auto profile=fixture.native();struct stat profileBefore{};QVERIFY(::stat(profile.c_str(),&profileBefore)==0);const auto mode=profileBefore.st_mode&07777;
    const auto restore=qScopeGuard([profile,mode]{(void)::chmod(profile.c_str(),mode);});QVERIFY(::chmod(profile.c_str(),0500)==0);
    request.request=3;request.generation=generation;request.name=QStringLiteral("Must never replay");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==3);QVERIFY(::chmod(profile.c_str(),mode)==0);
    QCOMPARE(result()->status,ui::ManualTagCommandStatus::Rejected);QVERIFY(!result()->writable&&result()->generation>generation);const auto failedGeneration=result()->generation;
    QCOMPARE(test::read(path),before);QVERIFY(!QFileInfo::exists(path+QStringLiteral(".saving"))&&!QFileInfo::exists(path+QStringLiteral(".bak")));
    request.request=4;request.kind=ui::ManualTagCommandKind::Reload;
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==4);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QVERIFY(result()->writable&&result()->generation>failedGeneration);QCOMPARE(result()->tags->tags.size(),std::size_t{1});QCOMPARE(test::read(path),before);
    request.request=5;request.generation=result()->generation;request.kind=ui::ManualTagCommandKind::Create;request.name=QStringLiteral("New explicit edit");
    QVERIFY(bridge.manualTagCommand(request));QTRY_VERIFY(!bridge.busy()&&commands.count()==5);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(result()->createdId,std::optional<qint32>{3});QCOMPARE(result()->tags->tags.size(),std::size_t{2});
    for(const auto& tag:result()->tags->tags){QVERIFY(tag.rawName!=std::optional<std::string>{"Must never replay"});}
  }
  void savedTagCommandDirectReceiverMayDeleteBridgeWhileBusy() {
    test::OwnedEngineFixture fixture(false);QObject cleanup;auto* bridge=new app::SessionBridge(&cleanup);QPointer<app::SessionBridge> alive(bridge);QSignalSpy sessions(bridge,&app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(bridge->open(fixture.native(),0));QTRY_VERIFY(!bridge->busy()&&!sessions.empty());int trailingIdle=0;bool deleted=false;
    connect(bridge,&app::SessionBridge::busyChanged,&cleanup,[&](bool busy){if(!busy){++trailingIdle;}});
    connect(bridge,&app::SessionBridge::manualTagCommandFinished,&cleanup,[&](auto packet){QVERIFY(bridge->busy());QCOMPARE(packet->status,ui::ManualTagCommandStatus::Loaded);delete bridge;deleted=true;});
    ui::ManualTagCommand request;request.session=sessions.last().at(0).toULongLong();request.request=1;
    QVERIFY(bridge->manualTagCommand(request));QTRY_VERIFY(deleted&&alive.isNull());QCOMPARE(trailingIdle,0);auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
  }
  void actualWindowWorkerTagWorkflowPreservesRawStateAcrossReopen() {
    // Only this generated Stopped row is opened. Port0 intentionally keeps the
    // Host engine absent; editable-window mode is a GUI hint, never a lease or
    // file permission. No start/continue/peer endpoint is submitted.
    test::OwnedEngineFixture fixture(false);
    const auto inventoryPath=fixture.profile+QStringLiteral("/downloads.config");
    auto inventory=test::read(inventoryPath);QCOMPARE(inventory.count("5:statei75e"),1);
    inventory.replace("5:statei75e","5:statei70e");test::write(inventoryPath,inventory);
    const auto path=fixture.profile+QStringLiteral("/tag.config");const auto original=tags(*fixture.hash);test::write(path,original);
    const auto state=test::read(fixture.stateFile);const auto payload=test::read(fixture.payload+'/'+fixture.files.front().relative);
    const auto preservesUnedited=[&](const QByteArray& bytes){
      auto before=core::Document::parse(original.toStdString()),after=core::Document::parse(bytes.toStdString());
      if(!before||!after){return false;}
      const auto same=[&](const core::Node* a,const core::Node* b){return a&&b&&before->originalBytes().substr(a->begin,a->end-a->begin)==after->originalBytes().substr(b->begin,b->end-b->begin);};
      if(!same(before->find("unknown-root"),after->find("unknown-root"))||!same(before->find("77"),after->find("77"))){return false;}
      const auto* a=before->find("3");const auto* b=after->find("3");if(!a||!b){return false;}
      for(const auto* key:{"c","1","2","3"}){if(!same(a->find(key),b->find(key))){return false;}}
      return true; // Includes the unresolved original member's exact raw ID.
    };
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy finished(&bridge,&app::SessionBridge::manualTagCommandFinished),statistics(&bridge,&app::SessionBridge::manualTagStatisticsReady);
    QSignalSpy closed(&bridge,&app::SessionBridge::sessionClosed),engineCommands(&bridge,&app::SessionBridge::engineStatisticsReady);
    const auto result=[&]{return finished.empty()?ui::ManualTagCommandPresentation{}:qvariant_cast<ui::ManualTagCommandPresentation>(finished.last().at(0));};
    // These are the production main_desktop connections, with the same guarded
    // submission/refusal and session-close handshake. No callback performs IO.
    const auto wire=[&](ui::LibraryWindow& window){
      using Bridge=app::SessionBridge;using Window=ui::LibraryWindow;
      window.setOfflinePreview(false);
      connect(&bridge,&Bridge::manualTagCommandFinished,&window,&Window::setManualTagCommandFinished);
      connect(&window,&Window::manualTagCommandRequested,&bridge,
        [owner=QPointer<Bridge>(&bridge),view=QPointer<Window>(&window)](ui::ManualTagCommand command){if(owner&&view&&!owner->manualTagCommand(command)&&view){view->rejectManualTagCommand(command);}});
      connect(&bridge,&Bridge::snapshotReady,&window,&Window::setPresentation);
      connect(&bridge,&Bridge::columnLayoutSessionReady,&window,&Window::beginColumnLayoutSession);
      connect(&bridge,&Bridge::busyChanged,&window,&Window::setBusy);
      connect(&bridge,&Bridge::messageReady,&window,&Window::setMessage);
      connect(&bridge,&Bridge::manualTagStatisticsReady,&window,&Window::setManualTagStatistics);
      connect(&window,&Window::manualTagStatisticsRequested,&bridge,&Bridge::manualTagStatistics);
      connect(&window,&Window::refreshRequested,&bridge,&Bridge::refresh);
      connect(&bridge,&Bridge::detailsReady,&window,&Window::setDetails);
      connect(&window,&Window::detailsRequested,&bridge,&Bridge::details);
      connect(&bridge,&Bridge::nativeSessionReady,&window,&Window::setNativeSession);
      connect(&window,&Window::cancelRequested,&bridge,&Bridge::cancel);
      window.enableSessionCloseHandshake();
      const QPointer<Window> view(&window);
      connect(&window,&Window::sessionCloseRequested,&bridge,[&,view]{if(view&&!bridge.closeSession()&&view){view->retrySessionClose();}});
      connect(&bridge,&Bridge::sessionClosed,&window,[view](quint64,bool,bool){if(view){view->completeSessionClose();}});
      connect(&bridge,&Bridge::sessionCloseFailed,&window,&Window::retrySessionClose);
    };
    qint32 createdId=0;quint64 firstSession=0;QByteArray committed;
    {
      ui::LibraryWindow window;wire(window);QSignalSpy intents(&window,&ui::LibraryWindow::manualTagCommandRequested);window.show();
      QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&sessions.count()==1,10000);
      firstSession=sessions.last().at(0).toULongLong();QVERIFY(firstSession);QCOMPARE(window.model().rowCount(),1);QVERIFY(window.model().targetAt(0));
      auto* torrents=window.findChild<QTableView*>(QStringLiteral("incomplete-torrents"));QVERIFY(torrents);QTRY_COMPARE(torrents->model()->rowCount(),1);torrents->setCurrentIndex(torrents->model()->index(0,0));torrents->selectRow(0);
      auto* manage=window.findChild<QAction*>(QStringLiteral("classic-manual-tags"));QVERIFY(manage);QTRY_VERIFY(!bridge.busy()&&manage->isEnabled());manage->trigger();
      QPointer<ui::ManualTagManagerDialog> dialog=window.findChild<ui::ManualTagManagerDialog*>();QVERIFY(dialog&&dialog->isVisible());
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==1,10000);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QCOMPARE(result()->session,firstSession);
      QCOMPARE(result()->request,qvariant_cast<ui::ManualTagCommand>(intents.last().at(0)).request);QVERIFY(result()->writable);QCOMPARE(result()->tags->source->originalBytes(),original.toStdString());
      auto* name=dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-name"));auto* create=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-new"));auto* table=dialog->findChild<QTableWidget*>(QStringLiteral("manual-tag-table"));QVERIFY(name&&create&&table);QCOMPARE(table->rowCount(),3);
      name->setText(QStringLiteral("E2E Tag"));QVERIFY(create->isEnabled());create->click();QVERIFY(bridge.busy());QVERIFY(dialog&&dialog->isVisible());QVERIFY(!create->isEnabled());QCOMPARE(table->rowCount(),3);
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==2,10000);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QVERIFY(result()->createdId&&*result()->createdId>0);createdId=*result()->createdId;QCOMPARE(table->rowCount(),4);
      QCOMPARE(result()->request,qvariant_cast<ui::ManualTagCommand>(intents.last().at(0)).request);QVERIFY(latest(statistics)&&latest(statistics)->tagRevision==result()->generation);
      auto* menu=window.findChild<QMenu*>(QStringLiteral("classic-torrent-tags"));QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));
      auto* member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-%1").arg(createdId));QVERIFY(member&&member->isEnabled()&&!member->isChecked());member->trigger();QVERIFY(bridge.busy());QVERIFY(!member->isChecked());
      const auto assign=qvariant_cast<ui::ManualTagCommand>(intents.last().at(0));QCOMPARE(assign.kind,ui::ManualTagCommandKind::AddMember);QCOMPARE(assign.member.hash,QString::fromStdString(fixture.hash->hex()));QCOMPARE(assign.member,target(rows));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==3,10000);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(result()->request,assign.request);
      const auto assigned=std::find_if(result()->tags->tags.begin(),result()->tags->tags.end(),[&](const auto& tag){return tag.id==createdId;});QVERIFY(assigned!=result()->tags->tags.end());QCOMPARE(assigned->uniqueHashes.size(),std::size_t{1});QVERIFY(assigned->uniqueHashes.front()==*fixture.hash);
      QCOMPARE(latest(statistics)->tagRevision,result()->generation);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-%1").arg(createdId));QVERIFY(member&&member->isChecked());
      int selected=-1;for(int row=0;row<table->rowCount();++row){if(table->item(row,0)->text()==QStringLiteral("E2E Tag")){selected=row;}}QVERIFY(selected>=0);table->setCurrentCell(selected,0);table->selectRow(selected);
      auto* rename=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-rename"));QVERIFY(rename);name->setText(QStringLiteral("Renamed E2E"));QVERIFY(rename->isEnabled());rename->click();QVERIFY(bridge.busy());QVERIFY(dialog&&dialog->isVisible());
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==4,10000);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Saved);QCOMPARE(result()->kind,ui::ManualTagCommandKind::Rename);
      const auto renamed=std::find_if(result()->tags->tags.begin(),result()->tags->tags.end(),[&](const auto& tag){return tag.id==createdId;});QVERIFY(renamed!=result()->tags->tags.end());QCOMPARE(renamed->rawName,std::optional<std::string>{"Renamed E2E"});QCOMPARE(renamed->uniqueHashes.size(),std::size_t{1});QVERIFY(renamed->uniqueHashes.front()==*fixture.hash);
      committed=test::read(path);QVERIFY(preservesUnedited(committed));QVERIFY(!QFileInfo::exists(path+QStringLiteral(".saving")));
      const auto stamp=tagStamp(path);auto* reload=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-reload"));QVERIFY(reload&&reload->isEnabled());reload->click();
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==5,10000);QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QCOMPARE(result()->tags->source->originalBytes(),committed.toStdString());QVERIFY(sameTag(stamp,path));QVERIFY(dialog&&dialog->isVisible());
      QCOMPARE(latest(statistics)->tagRevision,result()->generation);QCOMPARE(latest(statistics)->traffic.state,core::ManualTagTrafficState::Unavailable);QVERIFY(!latest(statistics)->traffic.target);QVERIFY(!latest(statistics)->traffic.completeOwnedCoverage);QCOMPARE(engineCommands.count(),0);
      window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last().at(1).toBool()&&closed.last().at(2).toBool());QTRY_VERIFY(!window.isVisible());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);
    }
    {auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);}
    {
      ui::LibraryWindow window;wire(window);QSignalSpy intents(&window,&ui::LibraryWindow::manualTagCommandRequested);window.show();
      QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);QVERIFY(sessions.last().at(0).toULongLong()>firstSession);
      auto* manage=window.findChild<QAction*>(QStringLiteral("classic-manual-tags"));QVERIFY(manage);QTRY_VERIFY(!bridge.busy()&&manage->isEnabled());manage->trigger();
      QPointer<ui::ManualTagManagerDialog> dialog=window.findChild<ui::ManualTagManagerDialog*>();QVERIFY(dialog);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==6,10000);
      QCOMPARE(result()->status,ui::ManualTagCommandStatus::Loaded);QCOMPARE(result()->session,sessions.last().at(0).toULongLong());QCOMPARE(result()->request,qvariant_cast<ui::ManualTagCommand>(intents.last().at(0)).request);QCOMPARE(result()->tags->source->originalBytes(),committed.toStdString());
      const auto saved=std::find_if(result()->tags->tags.begin(),result()->tags->tags.end(),[&](const auto& tag){return tag.id==createdId;});QVERIFY(saved!=result()->tags->tags.end());QCOMPARE(saved->rawName,std::optional<std::string>{"Renamed E2E"});QCOMPARE(saved->uniqueHashes.size(),std::size_t{1});QVERIFY(saved->uniqueHashes.front()==*fixture.hash);
      auto* menu=window.findChild<QMenu*>(QStringLiteral("classic-torrent-tags"));QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));const auto* member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-%1").arg(createdId));QVERIFY(member&&member->isChecked());
      window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);QVERIFY(closed.last().at(1).toBool()&&closed.last().at(2).toBool());QTRY_VERIFY(!window.isVisible());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);
    }
    {auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);}
    QCOMPARE(test::read(path),committed);QVERIFY(preservesUnedited(committed));QCOMPARE(test::read(inventoryPath),inventory);QCOMPARE(test::read(fixture.stateFile),state);QCOMPARE(test::read(fixture.payload+'/'+fixture.files.front().relative),payload);QVERIFY(fixture.intact(false));QCOMPARE(engineCommands.count(),0);
  }
};
QTEST_MAIN(ManualTagBridgeTests)
#include "manual_tag_bridge_tests.moc"
