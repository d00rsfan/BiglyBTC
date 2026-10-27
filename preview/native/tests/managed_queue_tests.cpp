#include "persistent_session.hpp"
#include "library_commands.hpp"
#include "file_policy_commands.hpp"
#include "owned_engine_fixture.hpp"
#include "verified_seeding_fixture.hpp"
#include "managed_queue_observations.hpp"
#include <thread>

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <sys/stat.h>
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
// Test-only syscall seam. Only the exact owned profile directory is matched;
// all real syncs execute. Fail the second directory-sync return (after primary
// publication), never a payload/background directory. No production fault API.
std::atomic<std::uint64_t> queueFaultDevice{0},queueFaultInode{0};
std::atomic<int> queueFaultRemaining{0},queueFaultHits{0};
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){
  const auto actual=__real_fsync(fd);if(actual!=0||queueFaultRemaining.load(std::memory_order_acquire)<=0){return actual;}
  struct stat value{};
  if(::fstat(fd,&value)==0&&S_ISDIR(value.st_mode)
      &&static_cast<std::uint64_t>(value.st_dev)==queueFaultDevice.load()
      &&static_cast<std::uint64_t>(value.st_ino)==queueFaultInode.load()){
    if(queueFaultRemaining.fetch_sub(1,std::memory_order_acq_rel)==1){++queueFaultHits;errno=EIO;return -1;}
  }
  return actual;
}
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

  void actualObserverQueueHasBoundedNonblockingProducerAndWorkerOnlyDrain(){
    engine::detail::ManagedQueueObservations observations(1);observations.installed();
    std::thread producer([&]{core::QueueObservation event;event.kind=core::QueueObservationKind::StartedDownload;event.sequence=1;event.observedNanos=1000000000;event.observedWallMillis=100000;observations.observe(event);});
    producer.join();const auto first=observations.facts(2000);QVERIFY(first.durations);QCOMPARE(first.durations->downloading,std::int64_t{1});
    std::atomic<bool> foreignHadFacts{false};std::thread foreignReader([&]{foreignHadFacts=observations.facts(2000).durations.has_value();});foreignReader.join();QVERIFY(!foreignHadFacts.load());
    QVERIFY(observations.facts(2000).durations); // A rejected reader cannot consume the source stream.
    std::thread overflow([&]{core::QueueObservation event;event.kind=core::QueueObservationKind::Finished;event.observedNanos=2000000000;event.observedWallMillis=101000;
      for(std::uint64_t i=0;i<=engine::detail::ManagedQueueObservations::Capacity;++i){event.sequence=i+2;observations.observe(event);}});
    overflow.join();QVERIFY(!observations.facts(2000).durations);QVERIFY(!observations.settledDurations());
  }

  void actualProviderLifecycleAndPayloadSendSmoothingRetireAndReopenIndependently_data(){QTest::addColumn<bool>("multi");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;}
  void actualProviderLifecycleAndPayloadSendSmoothingRetireAndReopenIndependently(){
    QFETCH(bool,multi);test::OwnedEngineFixture fixture(multi);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto lease=held(fixture);auto service=owner();auto p=policy(1,2);p.rankMode=core::QueueRankMode::None;p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;p.ignoreZeroPeers=false;p.firstPriorityIgnoreZeroPeers=false;
    QVERIFY(service->configureQueuePolicy(p));QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port()}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&current->complete&&current->retained&&current->queueSourceFacts&&current->queueSourceFacts->durations;})(),30000);
    const auto before=status(*service,*fixture.hash);QVERIFY(before&&before->queueSourceFacts);QVERIFY(before->received>0);const auto admission=before->admission;
    test::VerifiedSeedCase actual{fixture.root.path(),fixture.profile,fixture.payload,fixture.stateFile,*fixture.hash,fixture.files};
    // The receiver validates actual bytes from THIS listener/owner; no invented
    // upload values or replayed protocol samples can satisfy the source rate.
    QVERIFY(!test::receiveVerifiedSeed(*service,actual).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&current->admission==admission&&current->sent>before->sent&&current->queueSmoothedPayloadUploadRate&&*current->queueSmoothedPayloadUploadRate>0;})(),5000);
    QVERIFY(service->stopManaged(*fixture.hash));
    const auto stopping=status(*service,*fixture.hash);QVERIFY(stopping&&!stopping->queueSourceFacts&&!stopping->queueSmoothedPayloadUploadRate);
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&!current->retained&&current->phase==Phase::Stopped;})(),10000);
    const auto retired=status(*service,*fixture.hash);QVERIFY(retired&&!retired->queueSourceFacts&&!retired->queueSmoothedPayloadUploadRate);
    QVERIFY(service->startManaged(lease,{.hash=*fixture.hash}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&current->admission>admission&&current->complete&&current->queueSmoothedPayloadUploadRate;})(),10000);
    const auto fresh=status(*service,*fixture.hash);QVERIFY(fresh&&fresh->queueSourceFacts&&fresh->queueSourceFacts->durations);QCOMPARE(fresh->sent,std::int64_t{0});QCOMPARE(*fresh->queueSmoothedPayloadUploadRate,std::int64_t{0});
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(fixture.intact());
  }

  void verifiedSeedActualPostPrimaryFailureCannotReleaseOrReplay(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",false);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importVerifiedSeedCase(*lease,fixture));auto service=owner();
    QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));
    struct ResetFault{~ResetFault(){queueFaultRemaining=0;}} resetFault;
    struct stat profile{};QVERIFY(::fstat(lease->directoryHandle(),&profile)==0);queueFaultDevice=profile.st_dev;queueFaultInode=profile.st_ino;queueFaultHits=0;queueFaultRemaining=2;
    // Same existing owned fsync wrapper: real sync executes, then fail after
    // the primary rename. No payload/global operation is faulted.
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&[&]{const auto current=test::verifiedStatus(*service,fixture.hash);return current&&current->verifiedSeeding==engine::VerifiedSeedingPhase::Failed;}(),30000);
    queueFaultRemaining=0;QCOMPARE(queueFaultHits.load(),1);
    const auto current=test::verifiedStatus(*service,fixture.hash);QVERIFY(current&&current->failure&&!current->verificationHeld);QCOMPARE(current->received,std::int64_t{0});QCOMPARE(current->sent,std::int64_t{0});
    const auto actual=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(actual&&*actual);QCOMPARE((*actual)->state,storage::VerifiedSeedState::Released);
    const auto published=test::read(fixture.profile+"/downloads.config");
    for(int n=0;n<3;++n){QVERIFY(service->poll());QCOMPARE(test::read(fixture.profile+"/downloads.config"),published);QCOMPARE(test::verifiedStatus(*service,fixture.hash)->verifiedSeeding,engine::VerifiedSeedingPhase::Failed);}
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined);QVERIFY(test::verifiedSeedBytesIntact(fixture));
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),published);
  }
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
    QCOMPARE(integer(newRow,"position"),std::int64_t{1}); // Original one-item group fixup.
    for(std::size_t i=0;i<oldRow.children.size();i+=2){
      const auto key=oldRow.children[i].bytes;if(key=="forceStart"||key=="position"){continue;}
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
    QCOMPARE(integer(row,"state"),std::int64_t{75});QCOMPARE(integer(row,"position"),std::int64_t{1});
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
  void actualNewSeedNormalizesBothSavedGroups_data(){QTest::addColumn<bool>("top");QTest::newRow("new-seed-top")<<true;QTest::newRow("new-seed-end")<<false;}
  void actualNewSeedNormalizesBothSavedGroups(){
    QFETCH(bool,top);
    test::OwnedEngineFixture seed(false),download(true);
    QTRY_VERIFY_WITH_TIMEOUT(seed.ready()&&download.ready(),30000);
    for(const auto& file:seed.files){test::write(seed.payload+'/'+file.relative,file.expected);}
    merge(seed,download);
    edit(seed,*seed.hash,{{"position","i1e"},{"forceStart","i1e"},{"opaque.queue","li7e4:keepe"}});
    edit(seed,*download.hash,{{"position","i9e"},{"opaque.queue","li9e4:keepe"}});
    auto lease=held(seed);auto service=owner();auto p=policy(1,0);p.rankMode=core::QueueRankMode::None;
    p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;p.newSeedsMoveTop=top;p.retainForceStartOnComplete=true;p.ignoreZeroPeers=false;
    QVERIFY(service->configureQueuePolicy(p));
    QVERIFY(service->startManaged(lease,{.hash=*seed.hash,.forceStart=true,.manuallyPaused=true}));
    QVERIFY(service->startManaged(lease,{.hash=*download.hash,.ownedLoopbackPeerPort=download.port()}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto a=status(*service,*seed.hash),b=status(*service,*download.hash);
      return poll&&a&&b&&!a->failure&&!b->failure&&a->checked&&b->checked&&a->complete&&b->complete
          &&a->queuePosition==std::optional<std::uint32_t>{top?2U:1U}&&b->queuePosition==std::optional<std::uint32_t>{top?1U:2U}
          &&!a->queuePositionFailure&&!b->queuePositionFailure;
    })(),30000);
    const auto a=status(*service,*seed.hash),b=status(*service,*download.hash);QVERIFY(a&&b);
    QVERIFY(a->manuallyPaused&&a->forceStart&&a->received==0&&b->received>0);
    const auto saved=parsed(test::read(seed.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(saved,*seed.hash),"position"),std::int64_t{top?2:1});QCOMPARE(integer(entry(saved,*download.hash),"position"),std::int64_t{top?1:2});
    QCOMPARE(integer(entry(saved,*seed.hash),"forceStart"),std::int64_t{1});
    QCOMPARE(encoded(*entry(saved,*seed.hash).find("opaque.queue")),std::string("li7e4:keepe"));
    QCOMPARE(encoded(*entry(saved,*download.hash).find("opaque.queue")),std::string("li9e4:keepe"));
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(seed.intact(false));
    const auto final=parsed(test::read(seed.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(final,*seed.hash),"position"),std::int64_t{top?2:1});QCOMPARE(integer(entry(final,*download.hash),"position"),std::int64_t{top?1:2});
  }
  void laterAdmissionRestoresCompletePositionAuthorityWithoutReopening(){
    test::OwnedEngineFixture first(false),second(true);QTRY_VERIFY_WITH_TIMEOUT(first.ready()&&second.ready(),30000);merge(first,second);
    edit(first,*first.hash,{{"position","i3e"}});edit(first,*second.hash,{{"position","i8e"}});
    auto lease=held(first);auto service=owner();auto p=policy(1,0);p.rankMode=core::QueueRankMode::None;
    QVERIFY(service->configureQueuePolicy(p));QVERIFY(service->startManaged(lease,{.hash=*first.hash,.manuallyPaused=true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto value=status(*service,*first.hash);
      return poll&&value&&value->checked&&value->queuePositionFailure;
    })(),30000);
    const auto incomplete=status(*service,*first.hash);QVERIFY(incomplete&&!incomplete->failure);
    QCOMPARE(incomplete->queuePositionFailure->code,storage::ErrorCode::Unsupported);
    const auto unchanged=test::read(first.profile+QStringLiteral("/downloads.config"));
    for(int i=0;i<3;++i){QVERIFY(service->poll());const auto current=status(*service,*first.hash);QVERIFY(current&&current->queuePositionFailure==incomplete->queuePositionFailure);}
    QCOMPARE(test::read(first.profile+QStringLiteral("/downloads.config")),unchanged);
    QVERIFY(service->startManaged(lease,{.hash=*second.hash,.manuallyPaused=true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto a=status(*service,*first.hash),b=status(*service,*second.hash);
      return poll&&a&&b&&a->checked&&b->checked&&!a->failure&&!b->failure&&!a->queuePositionFailure&&!b->queuePositionFailure
          &&a->queuePosition==std::optional<std::uint32_t>{1}&&b->queuePosition==std::optional<std::uint32_t>{2};
    })(),30000);
    const auto a=status(*service,*first.hash),b=status(*service,*second.hash);QVERIFY(a&&b&&a->manuallyPaused&&b->manuallyPaused&&a->received==0&&b->received==0);
    const auto normalized=parsed(test::read(first.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(normalized,*first.hash),"position"),std::int64_t{1});QCOMPARE(integer(entry(normalized,*second.hash),"position"),std::int64_t{2});
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(first.intact(false));
  }
  void uncertainPublishedGroupTransitionNeverReplaysInSameOwner(){
    test::OwnedEngineFixture seed(false),download(true);QTRY_VERIFY_WITH_TIMEOUT(seed.ready()&&download.ready(),30000);
    for(const auto& file:seed.files){test::write(seed.payload+'/'+file.relative,file.expected);}merge(seed,download);
    edit(seed,*seed.hash,{{"position","i1e"},{"forceStart","i1e"}});edit(seed,*download.hash,{{"position","i1e"}});
    auto lease=held(seed);auto service=owner();auto p=policy(1,0);p.rankMode=core::QueueRankMode::None;
    p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;p.newSeedsMoveTop=true;p.retainForceStartOnComplete=true;p.ignoreZeroPeers=false;
    QVERIFY(service->configureQueuePolicy(p));
    QVERIFY(service->startManaged(lease,{.hash=*seed.hash,.forceStart=true,.manuallyPaused=true}));
    QVERIFY(service->startManaged(lease,{.hash=*download.hash,.ownedLoopbackPeerPort=download.port(),.manuallyPaused=true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto a=status(*service,*seed.hash),b=status(*service,*download.hash);
      return poll&&a&&b&&a->checked&&b->checked&&a->complete&&!b->complete&&!a->queuePositionFailure&&!b->queuePositionFailure;
    })(),30000);
    struct stat profile{};QVERIFY(::fstat(lease.directoryHandle(),&profile)==0);
    queueFaultDevice=static_cast<std::uint64_t>(profile.st_dev);queueFaultInode=static_cast<std::uint64_t>(profile.st_ino);queueFaultHits=0;queueFaultRemaining.store(2,std::memory_order_release);
    struct Disarm{~Disarm(){queueFaultRemaining=0;}} disarm;
    QVERIFY(service->pauseManaged(*download.hash,false));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto a=status(*service,*seed.hash),b=status(*service,*download.hash);
      return poll&&a&&b&&b->complete&&b->received>0&&a->queuePositionFailure&&b->queuePositionFailure;
    })(),30000);
    QCOMPARE(queueFaultHits.load(),1);const auto failed=status(*service,*download.hash);QVERIFY(failed&&failed->queuePositionFailure);
    QCOMPARE(failed->queuePositionFailure->code,storage::ErrorCode::Io);
    QVERIFY(failed->queuePositionFailure->message.find("replacement already occurred")!=std::string::npos);
    const auto published=parsed(test::read(seed.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(published,*seed.hash),"position"),std::int64_t{2});QCOMPARE(integer(entry(published,*download.hash),"position"),std::int64_t{1});
    // Subsequent explicit manual order is a new source decision. The uncertain
    // old completion event cannot overwrite it via configure/reorder triggers.
    edit(seed,*seed.hash,{{"position","i1e"}});edit(seed,*download.hash,{{"position","i2e"}});
    const auto manuallyOrdered=test::read(seed.profile+QStringLiteral("/downloads.config"));
    QVERIFY(service->configureQueuePolicy(p));const std::array<core::TorrentHash,2> order{*seed.hash,*download.hash};QVERIFY(service->reorderManaged(order));QVERIFY(service->poll());
    QCOMPARE(test::read(seed.profile+QStringLiteral("/downloads.config")),manuallyOrdered);
    const auto retained=status(*service,*download.hash);QVERIFY(retained&&retained->queuePositionFailure==failed->queuePositionFailure);
    QVERIFY((retained->unavailableQueueInputs&(1U<<5))!=0);
    queueFaultRemaining=0;
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    const auto final=parsed(test::read(seed.profile+QStringLiteral("/downloads.config")));
    QCOMPARE(integer(entry(final,*seed.hash),"position"),std::int64_t{1});QCOMPARE(integer(entry(final,*download.hash),"position"),std::int64_t{2});QVERIFY(seed.intact(false));
  }
  void actualTargetEtaIsAdmissionOwnedAndPauseClearsCurrentAuthority() {
    test::OwnedEngineFixture fixture(false,true);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto originalState=parsed(test::read(fixture.stateFile));
    auto lease=held(fixture);auto service=owner();auto settings=policy(1,1);settings.downloadOrder=core::QueueDownloadOrder::Eta;
    QVERIFY(service->configureQueuePolicy(settings,119));
    QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port()}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto observed=status(*service,*fixture.hash);
      return poll&&observed&&!observed->failure&&observed->phase==Phase::Downloading&&observed->received>0
          &&observed->pieces>0&&!observed->complete
          &&observed->queueTargetStatistics&&observed->queueTargetStatistics->etaSeconds
          &&observed->queueTargetStatistics->smoothedPayloadReceiveBytesPerSecond;
    })(),30000);
    const auto before=status(*service,*fixture.hash);QVERIFY(before&&before->queueTargetStatistics);
    const auto& actual=*before->queueTargetStatistics;QCOMPARE(actual.admission,before->admission);QVERIFY(actual.sequence>1);
    QCOMPARE(actual.smoothing.windowSeconds,119);QCOMPARE(actual.smoothing.intervalSeconds,1);
    QVERIFY(*actual.smoothedPayloadReceiveBytesPerSecond>=0);QVERIFY(*actual.etaSeconds>0);
    QVERIFY(service->pauseManaged(*fixture.hash,true));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto value=status(*service,*fixture.hash);
      return poll&&value&&value->phase==Phase::Paused&&!value->queueTargetStatistics;
    })(),15000);
    const auto paused=status(*service,*fixture.hash);QVERIFY(paused);QCOMPARE(paused->admission,before->admission);
    QVERIFY(service->queueManaged(*fixture.hash,false,false));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto value=status(*service,*fixture.hash);
      return poll&&value&&value->phase==Phase::Downloading&&value->queueTargetStatistics
          &&value->queueTargetStatistics->observedMillis>actual.observedMillis;
    })(),15000);
    auto position=settings;position.downloadOrder=core::QueueDownloadOrder::Position;
    QVERIFY(service->configureQueuePolicy(position,119));
    const auto unselected=status(*service,*fixture.hash);
    QVERIFY(unselected&&!unselected->queueTargetStatistics); // No poll to hide stale public facts.
    QVERIFY(service->configureQueuePolicy(settings,119));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto value=status(*service,*fixture.hash);
      return poll&&value&&value->phase==Phase::Downloading&&value->queueTargetStatistics
          &&value->queueTargetStatistics->observedMillis>actual.observedMillis;
    })(),15000);
    QVERIFY(service->stopManaged(*fixture.hash));
    const auto stopping=status(*service,*fixture.hash);
    QVERIFY(stopping&&stopping->phase==Phase::Stopping&&!stopping->queueTargetStatistics);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);
    const auto stopped=status(*service,*fixture.hash);
    QVERIFY(stopped&&!stopped->retained&&!stopped->queueTargetStatistics);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&closed.retirement.peerCounters);QVERIFY(!closed.checkpointFailure);
    QVERIFY(!stopped->failure&&stopped->phase==Phase::Stopped&&stopped->checked);
    QVERIFY(!stopped->complete&&!stopped->selectedComplete);
    constexpr int PieceBytes=16384;
    QCOMPARE(stopped->totalPieces,test::OwnedEngineFixture::PayloadBytes/PieceBytes);
    QVERIFY(stopped->pieces>0&&stopped->pieces<stopped->totalPieces);
    QVERIFY(stopped->received>=before->received&&stopped->sent>=before->sent);
    const auto verified=static_cast<std::int64_t>(stopped->pieces)*PieceBytes;
    QVERIFY(stopped->received>=verified);
    QVERIFY(closed.retirement.peerCounters->counters().payloadReceived>=stopped->received);
    QVERIFY(closed.retirement.peerCounters->counters().payloadSent>=stopped->sent);
    const auto saved=parsed(test::read(fixture.profile+QStringLiteral("/downloads.config")));
    const auto& row=entry(saved,*fixture.hash);
    QCOMPARE(integer(row,"downloaded"),stopped->received);QCOMPARE(integer(row,"uploaded"),stopped->sent);
    QCOMPARE(integer(row,"completedbytes"),verified);
    const auto state=parsed(test::read(fixture.stateFile));
    const auto* resume=state.find("resume");QVERIFY(resume);
    const auto* data=resume->find("data");QVERIFY(data);
    QCOMPARE(integer(*data,"valid"),std::int64_t{0});
    const auto* flags=data->find("resume data");QVERIFY(flags&&flags->type==core::Node::Type::Bytes);
    QCOMPARE(std::string(flags->bytes),std::string(static_cast<std::size_t>(stopped->totalPieces),'\2'));
    QVERIFY(state.find("opaque.fixture")&&originalState.find("opaque.fixture"));
    QCOMPARE(encoded(*state.find("opaque.fixture")),encoded(*originalState.find("opaque.fixture")));
    // This peer deliberately lacks the final piece. Verify actual received
    // blocks, not completion that the fixture can never supply. A good block
    // awaiting verification is permitted without claiming it as saved progress.
    QCOMPARE(fixture.files.size(),std::size_t{1});
    const auto& file=fixture.files.front();const auto payload=test::read(fixture.payload+'/'+file.relative);
    QCOMPARE(payload.size(),file.expected.size());int goodPieces=0;
    for(int i=0;i<stopped->totalPieces;++i){
      const auto block=payload.mid(i*PieceBytes,PieceBytes);
      if(block==file.expected.mid(i*PieceBytes,PieceBytes)){++goodPieces;}
      else{QCOMPARE(block,QByteArray(PieceBytes,'\0'));}
    }
    QVERIFY(goodPieces>=stopped->pieces);
    QCOMPARE(payload.last(PieceBytes),QByteArray(PieceBytes,'\0'));
    QVERIFY(fixture.intact(false)); // Original statistics, dev/inode and no temporary saves remain exact.
  }
  void actualOriginalFilePriorityProviderPreservesStartupAndManualHold() {
    test::OwnedEngineFixture single(false,true),multi(true,true);
    QTRY_VERIFY_WITH_TIMEOUT(single.ready()&&multi.ready(),30000);
    merge(single,multi);
    edit(single,*single.hash,{{"position","i2e"},{"opaque.priority","li7e4:keepe"}});
    edit(single,*multi.hash,{{"position","i1e"},{"opaque.priority","li9e4:keepe"}});
    auto lease=held(single);
    const std::array<compat::FilePolicy,1> normal{{{false,0}}};
    const std::array<compat::FilePolicy,2> high{{{false,2},{false,2}}};
    QVERIFY(storage::setFilePolicies(lease,*single.hash,normal));
    QVERIFY(storage::setFilePolicies(lease,*multi.hash,high));
    auto service=owner();auto settings=policy(1,1);settings.downloadOrder=core::QueueDownloadOrder::FilePriority;
    QVERIFY(service->configureQueuePolicy(settings));
    // Original sorting waits 30 seconds. Exercise the actual observation
    // provider and manual hold without sleeping or bypassing that gate; the
    // pure test proves priority overrides position after the exact boundary.
    QVERIFY(service->startManaged(lease,{.hash=*multi.hash,.ownedLoopbackPeerPort=multi.port()}));
    QVERIFY(service->startManaged(lease,{.hash=*single.hash,.ownedLoopbackPeerPort=single.port()}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto a=status(*service,*single.hash),b=status(*service,*multi.hash);
      return poll&&a&&b&&!a->failure&&!b->failure&&a->checked&&b->checked
          &&a->queueFilePriority&&b->queueFilePriority
          &&(*a->queueFilePriority)[0]==0&&(*a->queueFilePriority)[1]==0
          &&(*a->queueFilePriority)[2]==0&&(*a->queueFilePriority)[3]==0
          &&(*b->queueFilePriority)[0]==2&&(*b->queueFilePriority)[1]==2
          &&(*b->queueFilePriority)[2]==4&&(*b->queueFilePriority)[3]>=2
          &&a->phase==Phase::Queued&&b->phase==Phase::Downloading&&b->received>0;
    })(),30000);
    const auto waiting=status(*service,*single.hash);QVERIFY(waiting);
    QCOMPARE(waiting->received,std::int64_t{0});
    QVERIFY(service->pauseManaged(*multi.hash,true));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      const auto poll=service->poll();const auto a=status(*service,*single.hash),b=status(*service,*multi.hash);
      return poll&&a&&b&&!a->failure&&!b->failure&&b->phase==Phase::Paused&&b->manuallyPaused
          &&a->phase==Phase::Downloading&&a->received>0;
    })(),30000);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);
    const auto inventory=parsed(test::read(single.profile+QStringLiteral("/downloads.config")));
    const auto& first=entry(inventory,*single.hash);const auto& second=entry(inventory,*multi.hash);
    QVERIFY(first.find("opaque.priority")&&second.find("opaque.priority"));
    QCOMPARE(encoded(*first.find("opaque.priority")),std::string("li7e4:keepe"));
    QCOMPARE(encoded(*second.find("opaque.priority")),std::string("li9e4:keepe"));
    const auto savedFirst=compat::originalFilePolicy(first,1),savedSecond=compat::originalFilePolicy(second,2);
    QVERIFY(savedFirst&&savedSecond);QCOMPARE((*savedFirst)[0].priority,0);
    QCOMPARE((*savedSecond)[0].priority,2);QCOMPARE((*savedSecond)[1].priority,2);
    QVERIFY(!(*savedFirst)[0].skipped&&!(*savedSecond)[0].skipped&&!(*savedSecond)[1].skipped);
    QVERIFY(single.intact(false));
  }
};
QTEST_GUILESS_MAIN(ManagedQueueTests)
#include "managed_queue_tests.moc"
