#include "persistent_session.hpp"
#include "library_commands.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using Phase = engine::ManagedPhase;
using Reason = core::QueueReason;
std::unique_ptr<engine::PersistentSession> owner() {
  auto result=engine::PersistentSession::create({801,802,803});
  test::require(result.has_value(),"Queue tests require the approved source-bound provider");
  return std::move(*result);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto result=storage::ProfileLease::acquire(fixture.native());
  test::require(result.has_value(),"Cannot hold generated queue profile");return std::move(*result);
}
core::Document parsed(const QByteArray& bytes) {
  auto result=core::Document::parse(bytes.toStdString(),storage::compatibleConfigLimits());
  test::require(result.has_value(),"Cannot parse generated queue configuration");return std::move(*result);
}
std::string encoded(const core::Node& node) {
  auto value=core::encodeValue(node,storage::compatibleConfigLimits());
  test::require(value.has_value(),"Cannot own generated original subtree");return std::move(*value);
}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string bytes(const QString& value){return bytes(value.toUtf8().toStdString());}
std::string hashBytes(const core::TorrentHash& hash) {
  const auto raw=hash.bytes();return {reinterpret_cast<const char*>(raw.data()),raw.size()};
}
const core::Node& entry(const core::Document& document,const core::TorrentHash& hash) {
  const auto* rows=document.find("downloads");test::require(rows&&rows->type==core::Node::Type::List,"Missing generated inventory");
  const auto key=hashBytes(hash);
  for(const auto& row:rows->children){const auto* stored=row.find("torrent_hash");if(stored&&stored->type==core::Node::Type::Bytes&&stored->bytes==key){return row;}}
  throw std::runtime_error("Missing generated target");
}
std::int64_t integer(const core::Node& row,std::string_view key) {
  const auto* value=row.find(key);test::require(value&&value->type==core::Node::Type::Integer,"Missing generated integer");return value->integer;
}
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& service,const core::TorrentHash& hash) {
  const auto all=service.managedDownloads();const auto found=std::ranges::find(all,hash,&engine::ManagedDownloadStatus::hash);
  return found==all.end()?std::nullopt:std::optional{*found};
}
void edit(test::OwnedEngineFixture& fixture,const core::TorrentHash& hash,
    std::initializer_list<std::pair<std::string_view,std::string>> changes) {
  auto document=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
  const auto* list=document.find("downloads");test::require(list!=nullptr,"Missing generated rows");
  std::string replacement="l";
  for(const auto& row:list->children){
    auto own=core::Document::parse(encoded(row),storage::compatibleConfigLimits());test::require(own.has_value(),"Cannot own generated row");
    const auto* stored=row.find("torrent_hash");test::require(stored!=nullptr,"Missing generated hash");
    if(stored->bytes==hashBytes(hash)){
      for(const auto& [key,value]:changes){test::require(own->replaceValue(key,value).has_value(),"Cannot edit generated row");}
    }
    replacement+=own->serialize();
  }
  replacement+='e';test::require(document.replaceValue("downloads",replacement).has_value(),"Cannot replace generated rows");
  test::write(fixture.profile+QStringLiteral("/downloads.config"),QByteArray::fromStdString(document.serialize()));
}
// Reuse both fixture-generated hashes and exact metadata; only the second
// linear layout is rebound into the first generated held profile.
void merge(test::OwnedEngineFixture& first,const test::OwnedEngineFixture& second) {
  for(const auto& file:second.files){
    const auto path=first.payload+'/'+file.relative;test::write(path,QByteArray(file.expected.size(),'\0'));
    auto copy=file;test::require(::stat(path.toStdString().c_str(),&copy.before)==0,"Cannot stat merged payload");
    first.files.push_back(std::move(copy));
  }
  const auto state=first.profile+QStringLiteral("/active/")+QString::fromStdString(second.hash->hex())+QStringLiteral(".dat");
  test::write(state,test::read(second.stateFile));
  auto a=parsed(test::read(first.profile+QStringLiteral("/downloads.config")));
  auto b=parsed(test::read(second.profile+QStringLiteral("/downloads.config")));
  auto row=core::Document::parse(encoded(entry(b,*second.hash)),storage::compatibleConfigLimits());
  test::require(row.has_value(),"Cannot own second generated row");
  test::require(row->replaceValue("save_dir",bytes(first.payload)).has_value(),"Cannot rebind generated save directory");
  test::require(row->replaceValue("torrent",bytes(state)).has_value(),"Cannot rebind generated metadata");
  test::require(a.replaceValue("downloads","l"+encoded(entry(a,*first.hash))+row->serialize()+'e').has_value(),"Cannot merge generated rows");
  test::write(first.profile+QStringLiteral("/downloads.config"),QByteArray::fromStdString(a.serialize()));
}
core::QueuePolicy policy(int downloads,int active) {
  core::QueuePolicy p;p.maximumDownloads=downloads;p.maximumActive=active;p.minimumDownloads=0;return p;
}
} // namespace

class ManagedQueueTests final:public QObject {
  Q_OBJECT
private slots:
  void limitsPauseResumeAndForceUseRealRetainedHandles_data() {
    QTest::addColumn<int>("maximumDownloads");QTest::addColumn<int>("maximumActive");
    QTest::newRow("download-cap")<<1<<0;
    QTest::newRow("active-cap")<<2<<1;
  }
  void limitsPauseResumeAndForceUseRealRetainedHandles() {
    QFETCH(int,maximumDownloads);QFETCH(int,maximumActive);
    test::OwnedEngineFixture single(false,true),multi(true,true);
    QTRY_VERIFY_WITH_TIMEOUT(single.ready()&&multi.ready(),30000);
    QVERIFY(*single.hash!=*multi.hash);merge(single,multi);
    edit(single,*single.hash,{{"position","i2e"},{"opaque.queue","li7e4:keepe"}});
    edit(single,*multi.hash,{{"position","i1e"},{"opaque.queue","li9e4:keepe"}});
    auto lease=held(single);auto service=owner();const auto p=policy(maximumDownloads,maximumActive);
    QVERIFY(service->configureQueuePolicy(p));QVERIFY(service->queuePolicy());QVERIFY(*service->queuePolicy()==p);
    QVERIFY(service->startManaged(lease,{.hash=*single.hash,.ownedLoopbackPeerPort=single.port()}));
    QVERIFY(service->startManaged(lease,{.hash=*multi.hash,.ownedLoopbackPeerPort=multi.port()}));
    // Both are registered before the sole consumer is pumped. stop_when_ready
    // must prevent any payload from the lower saved-position selection.
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto a=status(*service,*single.hash),b=status(*service,*multi.hash);
      return poll&&a&&b&&!a->failure&&!b->failure&&a->checked&&b->checked
          &&a->phase==Phase::Queued&&b->phase==Phase::Downloading&&b->received>0;
    })(),30000);
    auto a=status(*service,*single.hash),b=status(*service,*multi.hash);QVERIFY(a&&b);
    QVERIFY(a->retained&&b->retained);QVERIFY(a->admission!=b->admission);
    QCOMPARE(a->received,std::int64_t{0});QVERIFY(a->queueReason==Reason::DownloadLimit);
    QVERIFY(!a->complete&&!b->complete);const auto secondBeforePause=b->received;
    QVERIFY(service->pauseManaged(*multi.hash,true));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto x=status(*service,*single.hash),y=status(*service,*multi.hash);
      return poll&&x&&y&&!x->failure&&!y->failure&&y->phase==Phase::Paused&&y->manuallyPaused
          &&x->phase==Phase::Downloading&&x->received>0;
    })(),30000);
    a=status(*service,*single.hash);b=status(*service,*multi.hash);QVERIFY(a&&b);
    QVERIFY(b->received>=secondBeforePause);QVERIFY(b->retained&&!b->complete);
    const auto firstPrefix=a->received;
    QVERIFY(service->pauseManaged(*multi.hash,false));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto x=status(*service,*single.hash),y=status(*service,*multi.hash);
      return poll&&x&&y&&!x->failure&&!y->failure&&x->phase==Phase::Queued
          &&y->phase==Phase::Downloading&&!y->manuallyPaused;
    })(),10000);
    // Persist user intent through the real adapter before the engine effect,
    // as the Host does. Forced downloads do not consume automatic slots.
    QVERIFY(storage::applyLibraryCommand(lease,*single.hash,core::DownloadAction::ForceStart));
    QVERIFY(service->queueManaged(*single.hash,true,false));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto x=status(*service,*single.hash),y=status(*service,*multi.hash);
      return poll&&x&&y&&!x->failure&&!y->failure&&x->phase==Phase::Downloading&&x->forceStart
          &&y->phase==Phase::Downloading;
    })(),10000);
    QVERIFY(service->pauseManaged(*single.hash,true));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto x=status(*service,*single.hash),y=status(*service,*multi.hash);
      return poll&&x&&y&&!x->failure&&!y->failure&&x->phase==Phase::Paused&&x->manuallyPaused
          &&x->forceStart&&y->phase==Phase::Downloading;
    })(),10000);
    a=status(*service,*single.hash);b=status(*service,*multi.hash);QVERIFY(a&&b);
    QVERIFY(a->received>=firstPrefix&&b->received>0&&!a->complete&&!b->complete);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);
    a=status(*service,*single.hash);b=status(*service,*multi.hash);QVERIFY(a&&b&&!a->failure&&!b->failure);
    QCOMPARE(a->phase,Phase::Stopped);QCOMPARE(b->phase,Phase::Stopped);
    const auto saved=parsed(test::read(single.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(saved,*single.hash),"downloaded"),a->received);
    QCOMPARE(integer(entry(saved,*multi.hash),"downloaded"),b->received);
    QCOMPARE(integer(entry(saved,*single.hash),"forceStart"),std::int64_t{1});
    QCOMPARE(integer(entry(saved,*single.hash),"position"),std::int64_t{2});
    QCOMPARE(integer(entry(saved,*multi.hash),"position"),std::int64_t{1});
    QVERIFY(entry(saved,*single.hash).find("opaque.queue")&&entry(saved,*multi.hash).find("opaque.queue"));
    QCOMPARE(encoded(*entry(saved,*single.hash).find("opaque.queue")),std::string("li7e4:keepe"));
    QCOMPARE(encoded(*entry(saved,*multi.hash).find("opaque.queue")),std::string("li9e4:keepe"));
    QVERIFY(single.intact(false));
  }
  void completionRatioQueuesAndClearsOnlyForceBeforeActualCheckpoint() {
    test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    edit(fixture,*fixture.hash,{{"state","i75e"},{"forceStart","i1e"},{"position","i8e"},
        {"downloaded","i1000e"},{"uploaded","i500000e"},{"discarded","i100e"},{"hashfailbytes","i50e"},
        {"secondsDownloading","i71e"},{"secondsOnlySeeding","i83e"},
        {"opaque.queue","li7e4:keepe"}});
    const auto before=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
    auto lease=held(fixture);auto service=owner();auto p=policy(1,1);
    p.rankMode=core::QueueRankMode::None;p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;
    p.ignoreShareRatio=1000;p.retainForceStartOnComplete=false;
    QVERIFY(service->configureQueuePolicy(p));
    QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port(),.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto s=status(*service,*fixture.hash);
      return poll&&s&&!s->failure&&s->checked&&s->complete&&s->retained
          &&s->phase==Phase::Queued&&!s->forceStart&&s->queueReason==Reason::IgnoreShareRatio;
    })(),30000);
    const auto completed=status(*service,*fixture.hash);QVERIFY(completed&&completed->seedingRank);
    QCOMPARE(*completed->seedingRank,std::int32_t{-8});QVERIFY(completed->received>0);QVERIFY(fixture.intact());
    const auto cleared=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
    const auto& oldRow=entry(before,*fixture.hash);const auto& newRow=entry(cleared,*fixture.hash);
    QCOMPARE(oldRow.children.size(),newRow.children.size());QCOMPARE(integer(newRow,"forceStart"),std::int64_t{0});
    for(std::size_t i=0;i<oldRow.children.size();i+=2){
      const auto key=oldRow.children[i].bytes;if(key=="forceStart"){continue;}
      const auto* actual=newRow.find(key);QVERIFY(actual);QCOMPARE(encoded(*actual),encoded(oldRow.children[i+1]));
    }
    QVERIFY(service->stopManaged(*fixture.hash));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto s=status(*service,*fixture.hash);return poll&&s&&!s->failure&&s->phase==Phase::Stopped;})(),10000);
    const auto stopped=status(*service,*fixture.hash);QVERIFY(stopped&&!stopped->retained);
    const auto saved=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
    const auto& row=entry(saved,*fixture.hash);
    QCOMPARE(integer(row,"downloaded"),std::int64_t{1000}+stopped->received);
    QCOMPARE(integer(row,"uploaded"),std::int64_t{500000}+stopped->sent);
    QVERIFY(integer(row,"secondsDownloading")>=71&&integer(row,"secondsOnlySeeding")>=83);
    QCOMPARE(integer(row,"state"),std::int64_t{75});QCOMPARE(integer(row,"position"),std::int64_t{8});
    QCOMPARE(integer(row,"forceStart"),std::int64_t{0});
    QVERIFY(row.find("opaque.queue")&&oldRow.find("opaque.queue"));
    QCOMPARE(encoded(*row.find("opaque.queue")),encoded(*oldRow.find("opaque.queue")));
    const auto downloaded=integer(row,"downloaded"),uploaded=integer(row,"uploaded");
    const auto downloadTime=integer(row,"secondsDownloading"),seedTime=integer(row,"secondsOnlySeeding");
    // Rechecking actual complete files is not restored network traffic. The
    // newly loaded saved ratio can queue them without any artificial wall wait.
    QVERIFY(service->startManaged(lease,{.hash=*fixture.hash}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto s=status(*service,*fixture.hash);
      return poll&&s&&!s->failure&&s->checked&&s->complete&&s->phase==Phase::Queued
          &&s->queueReason==Reason::IgnoreShareRatio;
    })(),30000);
    const auto reopened=status(*service,*fixture.hash);QVERIFY(reopened);QVERIFY(reopened->admission>completed->admission);
    QCOMPARE(reopened->received,std::int64_t{0});QCOMPARE(reopened->sent,std::int64_t{0});
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    const auto final=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
    const auto& finalRow=entry(final,*fixture.hash);
    QCOMPARE(integer(finalRow,"downloaded"),downloaded);QCOMPARE(integer(finalRow,"uploaded"),uploaded);
    QVERIFY(integer(finalRow,"secondsDownloading")>=downloadTime&&integer(finalRow,"secondsOnlySeeding")>=seedTime);
    QCOMPARE(integer(finalRow,"forceStart"),std::int64_t{0});
    QVERIFY(finalRow.find("opaque.queue"));
    QCOMPARE(encoded(*finalRow.find("opaque.queue")),encoded(*oldRow.find("opaque.queue")));QVERIFY(fixture.intact());
  }
};
QTEST_GUILESS_MAIN(ManagedQueueTests)
#include "managed_queue_tests.moc"
