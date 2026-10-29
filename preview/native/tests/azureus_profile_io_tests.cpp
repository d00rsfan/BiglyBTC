// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_azureus_service.hpp"
#include "azureus_profile_io.hpp"
#include <QDateTime>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QDir>
#include <QCryptographicHash>
#include <algorithm>
#include <vector>
#include <future>
#include <thread>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using Io=app::detail::AzureusProfileIo;
namespace udp=services::azureus_udp;
bool terminal(const app::OwnedAzureusService& owner,std::uint64_t request){const auto v=owner.observation();return v.lastResult&&v.lastResult->request==request&&v.lastResult->terminal;}
bool successful(const app::OwnedAzureusService& owner,std::uint64_t request){const auto v=owner.observation();return v.lastResult&&v.lastResult->request==request&&!v.lastResult->error&&!v.lastResult->transportFailure&&v.lastResult->terminal;}
bool phase(const Io::Handle& job,Io::Phase expected){std::lock_guard lock(job->mutex);return job->phase==expected;}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot read owned IO fixture");}return file.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes)!=bytes.size()||!file.flush()){throw std::runtime_error("Cannot write owned IO fixture");}}
struct Normal {
  QTemporaryDir root;
  std::optional<storage::ProfileLease> lease;
  std::shared_ptr<storage::AzureusValueVersions> versions;
  std::unique_ptr<app::OwnedAzureusService> owner;
  explicit Normal(const char* address,std::int32_t network=4,udp::RoutingScope scope=udp::RoutingScope::ExactPeers){
    if(!root.isValid()){throw std::runtime_error("Profile fixture");}
    auto acquired=storage::ProfileLease::acquire(root.path().toStdString());if(!acquired){throw std::runtime_error("Profile lock fixture");}lease.emplace(std::move(*acquired));
    auto source=storage::ConfigFile::createMissing(*lease,"biglybt.config",std::string("d22:Plugin.DHT.dht.enabledi1e6:future1048576:")+std::string(1048576,'x')+"e");if(!source){throw std::runtime_error("Config fixture");}
    auto opened=storage::AzureusValueVersions::open(*lease,network);if(!opened){throw std::runtime_error("Version fixture");}versions=*opened;
    app::AzureusServiceOptions options;options.network=network;options.routingScope=scope;options.enabled=true;options.servePings=true;options.serveValues=true;options.valueVersions=versions;options.bindAddress=QHostAddress(QString::fromLatin1(address));
    auto started=app::OwnedAzureusService::create(options);if(!started){throw std::runtime_error("Normal IO service fixture");}owner=std::move(*started);
  }
};
std::string encoded(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dictionary(std::initializer_list<std::pair<std::string,std::string>> entries){
  std::vector<std::pair<std::string,std::string>> values(entries);std::ranges::sort(values);std::string result="d";for(const auto& [key,value]:values){result+=encoded(key)+value;}return result+'e';
}
storage::DistributedRatingSnapshot installRating(Normal& normal){
  if(!QDir(normal.root.path()).mkdir("active")){throw std::runtime_error("Cannot create owned rating state");}
  const auto info=dictionary({{"length","i3e"},{"name",encoded("io-rating.bin")},{"piece length","i16384e"},{"pieces",encoded(std::string(20,'x'))}});
  const auto digest=QCryptographicHash::hash(QByteArray(info.data(),static_cast<qsizetype>(info.size())),QCryptographicHash::Sha1);
  auto hash=core::TorrentHash::parse(std::string_view(digest.constData(),static_cast<std::size_t>(digest.size())));if(!hash){throw std::runtime_error("Cannot derive owned rating identity");}
  const auto bytes=hash->bytes();
  const auto inventory=dictionary({{"downloads","l"+dictionary({{"state","i70e"},{"save_dir",encoded("/owned/io-rating-payload-never-opened")},{"torrent",encoded("owned-rating.torrent")},{"torrent_hash",encoded(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()))}})+"e"}});
  const auto attributes=dictionary({{"Plugin.azrating.comment",encoded("owned IO fallback")},{"Plugin.azrating.rating",encoded("4")},{"flags","i0e"},{"networks","l"+encoded("Public")+"e"}});
  const auto state=dictionary({{"attributes",attributes},{"info",info},{"resume",encoded("retained-resume")}});
  write(normal.root.path()+"/downloads.config",QByteArray(inventory.data(),static_cast<qsizetype>(inventory.size())));
  write(normal.root.path()+"/active/"+QString::fromStdString(hash->hex())+".dat",QByteArray(state.data(),static_cast<qsizetype>(state.size())));
  auto result=storage::readDistributedRating(*normal.lease,*hash);if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);
}
int observerCalls=0;
void countedObserver(storage::SaveStage){++observerCalls;}
struct IoWakeCounter final:QObject {
  explicit IoWakeCounter(Io& value):io(value){}
  bool event(QEvent* value)override{
    if(value->type()==Io::wakeEvent()){io.drainWake();++wakes;return true;}
    return QObject::event(value);
  }
  Io& io;
  int wakes=0;
};
struct HeldIo {
  Io io;
  Io::Handle job;
  std::thread::id worker;
  std::size_t sourceBytes=0;
  explicit HeldIo(const std::shared_ptr<storage::AzureusValueVersions>& versions):io(versions->profileOwnership()){
    auto started=io.submit([this,versions](std::stop_token stop)->Io::Result{
      worker=std::this_thread::get_id();if(stop.stop_requested()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Cancelled owned IO"});}
      auto current=versions->validate();if(!current){return std::unexpected(current.error());}
      auto source=storage::ConfigFile::load(*versions->profileOwnership(),"biglybt.config");if(!source){return std::unexpected(source.error());}sourceBytes=source->document().originalBytes().size();
      auto next=versions->next(QDateTime::currentMSecsSinceEpoch());if(!next){return std::unexpected(next.error());}
      Io::Data data;data.version=*next;return data;
    });if(!started){throw std::runtime_error("Cannot submit owned IO hold");}job=*started;
  }
  // Deliberately hold AFTER an actual source read and durable reservation,
  // before one-shot commit. This is not a claim of a blocked kernel syscall.
  void release(){if(!job){return;}if(auto ready=Io::take(job)){Io::acknowledge(job);}else{Io::revoke(job);}}
  ~HeldIo(){release();io.requestStop();io.join();}
};
}

class AzureusProfileIoTests final:public QObject {
  Q_OBJECT
private slots:
  void heldDiskHandoffDoesNotBlockRealPingDeadlineCancellationOrSocketClose(){
    Normal a("127.41.0.1"),b("127.42.0.1");const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
    HeldIo held(a.versions);QTRY_VERIFY_WITH_TIMEOUT(phase(held.job,Io::Phase::Ready),3000);QVERIFY(held.worker!=std::this_thread::get_id());QVERIFY(held.sourceBytes>1048576);
    const auto pending=a.owner->publishValue(b.owner->endpoint(),"held-io-key","no-store-before-permit",5s);QVERIFY(pending);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().profileIoRunning==1,3000);
    QElapsedTimer progress;progress.start();const auto ping=a.owner->ping(b.owner->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*ping),3000);const auto pingMillis=progress.elapsed();
    QUdpSocket silent;QVERIFY(silent.bind(QHostAddress(QStringLiteral("127.43.0.1")),0,QUdpSocket::DontShareAddress));
    const auto timeout=a.owner->findValue({silent.localAddress(),silent.localPort()},{},0,1,30ms);QVERIFY(timeout);QTRY_VERIFY_WITH_TIMEOUT(terminal(*a.owner,*timeout),3000);QCOMPARE(a.owner->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});
    const auto cancel=a.owner->cancel(*pending);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().publishSnapshot&&a.owner->observation().publishSnapshot->request==*pending&&a.owner->observation().publishSnapshot->state==app::AzureusPublishState::Cancelled,3000);
    const auto expiring=a.owner->publishValue(b.owner->endpoint(),"expired-io-key","must-not-send",150ms);QVERIFY(expiring);
    QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().publishSnapshot&&a.owner->observation().publishSnapshot->request==*expiring&&a.owner->observation().publishSnapshot->state==app::AzureusPublishState::Failed,3000);QCOMPARE(a.owner->observation().publishSnapshot->transportFailure,std::optional{udp::Failure::Timeout});QCOMPARE(a.owner->observation().databaseSnapshot->localPuts,std::uint64_t{0});
    const auto endpoint=a.owner->endpoint();const auto sent=a.owner->observation().snapshot->transport().counters().sent;auto closed=std::async(std::launch::async,[&]{return a.owner->close();});const auto releaseOnFailure=qScopeGuard([&]{held.release();});
    QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().state==app::AzureusServiceState::Closed,3000);QVERIFY(!a.owner->observation().joined);QVERIFY(closed.wait_for(0ms)!=std::future_status::ready);
    QUdpSocket replacement;QVERIFY(replacement.bind(endpoint.address,endpoint.port,QUdpSocket::DontShareAddress));held.release();QTRY_VERIFY_WITH_TIMEOUT(closed.wait_for(0ms)==std::future_status::ready,3000);QVERIFY(closed.get());QVERIFY(a.owner->observation().joined);QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,sent);QCOMPARE(a.owner->observation().databaseSnapshot->localPuts,std::uint64_t{0});QVERIFY(b.owner->close());qInfo("Held post-read/reservation IO: source=%zuB ping=%lldms total-through-close=%lldms, sockets closed before external IO join",held.sourceBytes,static_cast<long long>(pingMillis),static_cast<long long>(progress.elapsed()));
  }
  void currentVersionSourceIsRecheckedAfterTheHeldJobBeforeAnyPacketEffect(){
    Normal a("127.44.0.1"),b("127.45.0.1");const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
    HeldIo held(a.versions);QTRY_VERIFY_WITH_TIMEOUT(phase(held.job,Io::Phase::Ready),3000);const auto request=a.owner->publishValue(b.owner->endpoint(),"changed-before-validation","unchanged-body");QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().profileIoRunning==1,3000);
    // An external write BEFORE the service's queued check executes, not an
    // unsupported atomic hostile swap during a consumed permit.
    const auto path=a.root.path()+"/dht/net4/version.dat";const QByteArray foreign("d4:nexti7e6:future7:foreigne");write(path,foreign);held.release();
    QTRY_VERIFY_WITH_TIMEOUT(terminal(*a.owner,*request),3000);const auto observation=a.owner->observation();QVERIFY(observation.publishSnapshot);QCOMPARE(observation.publishSnapshot->state,app::AzureusPublishState::Failed);QCOMPARE(observation.publishSnapshot->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(observation.snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(observation.databaseSnapshot->localPuts,std::uint64_t{0});QCOMPARE(read(path),foreign);QVERIFY(a.owner->close());QVERIFY(b.owner->close());
  }
  void initialRatingIoCanBeCancelledBeforeAnySnapshotOrPacketAndFreshReadStillWorks(){
    Normal a("127.46.0.1",0),b("127.47.0.1",0);const auto source=installRating(a);
    const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
    HeldIo held(a.versions);QTRY_VERIFY_WITH_TIMEOUT(phase(held.job,Io::Phase::Ready),3000);
    const auto request=a.owner->readSavedRatings(b.owner->endpoint(),source.local.hash,source.sourceRevision);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().profileIoRunning==1,3000);
    const auto cancelled=a.owner->cancel(*request);QVERIFY(cancelled);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*cancelled)&&a.owner->observation().outstandingCommands==0,3000);
    QVERIFY(!a.owner->observation().ratingReadSnapshot);QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});
    held.release();QTRY_COMPARE_WITH_TIMEOUT(a.owner->observation().profileIoPending,std::size_t{0},3000);QVERIFY(!a.owner->observation().ratingReadSnapshot);QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});
    const auto fresh=a.owner->readSavedRatings(b.owner->endpoint(),source.local.hash,source.sourceRevision);QVERIFY(fresh);QTRY_VERIFY_WITH_TIMEOUT(terminal(*a.owner,*fresh),3000);
    const auto observed=a.owner->observation().ratingReadSnapshot;QVERIFY(observed);QCOMPARE(observed->request,*fresh);QCOMPARE(observed->state,app::AzureusRatingReadState::Complete);QVERIFY(observed->current&&observed->localFallback);QCOMPARE(observed->receivedValues,std::size_t{0});QCOMPARE(observed->ratings.average(),4.0F);
    QVERIFY(a.owner->close());QVERIFY(b.owner->close());
  }
  void nestedObservedWritersRefuseBeforeEffectsRatherThanCallUnderOuterGate(){
    QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);
    auto created=storage::ConfigFile::createMissing(*lease,"owned.config","d4:porti7ee");QVERIFY(created);QVERIFY(created->document().setInteger("port",8));const auto original=read(root.path()+"/owned.config");observerCalls=0;
    {
      auto outer=lease->sourceAccess();QVERIFY(outer);storage::ProfileSourceAccess moved(std::move(*outer));QVERIFY(moved.observationsAllowed());
      bool observerThrew=false;try{moved.outside([]{throw std::runtime_error("owned observer fault");});}catch(const std::runtime_error&){observerThrew=true;}QVERIFY(observerThrew&&moved.observationsAllowed());
      auto saved=created->save(*lease,countedObserver);QVERIFY(!saved);QCOMPARE(saved.error().code,storage::ErrorCode::Unsupported);
      auto first=storage::ConfigFile::createMissing(*lease,"new.config","de",storage::compatibleConfigLimits(),countedObserver);QVERIFY(!first);QCOMPARE(first.error().code,storage::ErrorCode::Unsupported);
      QCOMPARE(observerCalls,0);QCOMPARE(read(root.path()+"/owned.config"),original);QVERIFY(!QFile::exists(root.path()+"/owned.config.saving"));QVERIFY(!QFile::exists(root.path()+"/new.config"));QVERIFY(!QFile::exists(root.path()+"/new.config.saving"));
    }
    QVERIFY(created->save(*lease,countedObserver));QCOMPARE(observerCalls,3);QVERIFY(read(root.path()+"/owned.config")!=original);
  }
  void realNormalRepliesBackpressureWithinEightSlotsAndWithdrawOnSourceOrClose_data(){
    QTest::addColumn<int>("change");QTest::newRow("release-current")<<0;QTest::newRow("disable-current-source")<<1;QTest::newRow("close-and-cancel-queued")<<2;
  }
  void realNormalRepliesBackpressureWithinEightSlotsAndWithdrawOnSourceOrClose(){
    QFETCH(int,change);Normal a("127.48.0.1"),b("127.49.0.1",4,udp::RoutingScope::OwnedLoopback);const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
    auto access=b.lease->sourceAccess(true);QVERIFY(access);std::optional<storage::ProfileSourceAccess> blocked;blocked.emplace(std::move(*access));const auto release=qScopeGuard([&]{blocked.reset();});
    std::vector<std::uint64_t> requests;for(int i=0;i<12;++i){auto sent=a.owner->ping(b.owner->endpoint(),change==1?1s:5s);QVERIFY(sent);requests.push_back(*sent);}
    const auto heldState=[&]{const auto av=a.owner->observation(),bv=b.owner->observation();const auto& ac=av.snapshot->transport().counters();const auto& bc=bv.snapshot->transport().counters();return QString("receiver requests=%1 pending=%2 running=%3 ready=%4 highWater=%5 sent=%6; sender sent=%7 timeouts=%8 outstanding=%9").arg(static_cast<qulonglong>(bc.requests)).arg(static_cast<qulonglong>(bv.profileIoPending)).arg(static_cast<qulonglong>(bv.profileIoRunning)).arg(static_cast<qulonglong>(bv.profileIoReady)).arg(static_cast<qulonglong>(bv.profileIoHighWater)).arg(static_cast<qulonglong>(bc.sent)).arg(static_cast<qulonglong>(ac.sent)).arg(static_cast<qulonglong>(ac.timeouts)).arg(static_cast<qulonglong>(av.outstandingCommands));};
    QTRY_VERIFY2_WITH_TIMEOUT(b.owner->observation().snapshot->transport().counters().requests==12&&b.owner->observation().profileIoPending==8,qPrintable(heldState()),3000);QCOMPARE(b.owner->observation().profileIoHighWater,std::size_t{8});QCOMPARE(b.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(b.owner->observation().snapshot->transport().counters().serviceCapacityDrops,std::uint64_t{0});
    if(change==2){const auto endpoint=b.owner->endpoint();auto closed=std::async(std::launch::async,[&]{return b.owner->close();});const auto releaseBeforeJoin=qScopeGuard([&]{blocked.reset();});QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().state==app::AzureusServiceState::Closed,3000);QVERIFY(!b.owner->observation().joined);QUdpSocket rebound;QVERIFY(rebound.bind(endpoint.address,endpoint.port,QUdpSocket::DontShareAddress));for(auto request:requests){QVERIFY(a.owner->cancel(request));}QTRY_COMPARE_WITH_TIMEOUT(a.owner->observation().outstandingCommands,std::size_t{0},3000);blocked.reset();QTRY_VERIFY_WITH_TIMEOUT(closed.wait_for(0ms)==std::future_status::ready,3000);QVERIFY(closed.get());QCOMPARE(b.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(a.owner->observation().snapshot->transport().counters().replies,std::uint64_t{0});QVERIFY(a.owner->close());return;}
    if(change==1){auto config=storage::ConfigFile::load(*b.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*b.lease));}
    blocked.reset();QTRY_COMPARE_WITH_TIMEOUT(a.owner->observation().outstandingCommands,std::size_t{0},4000);QTRY_COMPARE_WITH_TIMEOUT(b.owner->observation().profileIoPending,std::size_t{0},4000);
    const auto av=a.owner->observation(),bv=b.owner->observation();QCOMPARE(bv.profileIoHighWater,std::size_t{8});QCOMPARE(bv.snapshot->transport().counters().serviceCapacityDrops,std::uint64_t{0});
    if(change==0){QCOMPARE(av.snapshot->transport().counters().replies,std::uint64_t{12});QCOMPARE(av.snapshot->transport().counters().timeouts,std::uint64_t{0});QCOMPARE(bv.snapshot->transport().counters().sent,std::uint64_t{12});QVERIFY(av.lastResult&&!av.lastResult->error&&!av.lastResult->transportFailure);}
    else{QCOMPARE(av.snapshot->transport().counters().replies,std::uint64_t{0});QCOMPARE(av.snapshot->transport().counters().timeouts,std::uint64_t{12});QCOMPARE(bv.snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(bv.snapshot->transport().counters().authorityErrors,std::uint64_t{12});}
    QVERIFY(a.owner->close());QVERIFY(b.owner->close());
  }
  void runningWorkerWakesQueueBackpressureBeforeHeldSourceCanBecomeReady(){
    QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,4);QVERIFY(versions);
    Io io((*versions)->profileOwnership());IoWakeCounter wake(io);io.bind(&wake);const auto unbind=qScopeGuard([&]{io.unbind();});
    auto access=lease->sourceAccess(true);QVERIFY(access);std::optional<storage::ProfileSourceAccess> blocked;blocked.emplace(std::move(*access));Io::Handle job;
    const auto release=qScopeGuard([&]{if(job){Io::revoke(job);}blocked.reset();});
    auto accepted=io.submit([v=*versions](std::stop_token)->Io::Result{auto valid=v->validate();if(!valid){return std::unexpected(valid.error());}return Io::Data{};});QVERIFY(accepted);job=*accepted;
    QTRY_VERIFY_WITH_TIMEOUT(phase(job,Io::Phase::Running),3000);QTRY_VERIFY_WITH_TIMEOUT(wake.wakes>0,3000);
    QVERIFY(!Io::take(job));const auto held=io.facts();QCOMPARE(held.pending,std::size_t{1});QCOMPARE(held.running,std::size_t{1});QCOMPARE(held.ready,std::size_t{0});
    blocked.reset();QTRY_VERIFY_WITH_TIMEOUT(phase(job,Io::Phase::Ready),3000);auto ready=Io::take(job);QVERIFY(ready&&*ready);Io::acknowledge(job);QTRY_VERIFY_WITH_TIMEOUT(Io::released(job),3000);io.release(job);io.requestStop();io.join();
  }
  void permitsAreSingleUseBoundQueuedActiveAndReadyThenReclaimSlots(){
    QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,4);QVERIFY(versions);Io io((*versions)->profileOwnership());
    std::array<Io::Handle,8> jobs;
    for(std::size_t i=0;i<jobs.size();++i){auto accepted=io.submit([v=*versions](std::stop_token)->Io::Result{auto valid=v->validate();if(!valid){return std::unexpected(valid.error());}return Io::Data{};});QVERIFY(accepted);jobs[i]=*accepted;if(i==0){QTRY_VERIFY_WITH_TIMEOUT(phase(jobs[0],Io::Phase::Ready),3000);}}
    QCOMPARE(io.highWater(),std::size_t{8});const auto excess=io.submit([](std::stop_token)->Io::Result{return Io::Data{};});QVERIFY(!excess);QCOMPARE(excess.error(),core::SubmitError::Overloaded);
    auto first=Io::take(jobs[0]);QVERIFY(first&&*first);QCOMPARE(first->value().revision,lease->sourceRevision());QVERIFY(!Io::take(jobs[0]));Io::revoke(jobs[0]);QVERIFY(!phase(jobs[0],Io::Phase::Released));Io::acknowledge(jobs[0]);QTRY_VERIFY_WITH_TIMEOUT(Io::released(jobs[0]),3000);QVERIFY(!Io::take(jobs[0]));io.release(jobs[0]);
    for(std::size_t i=1;i<jobs.size();++i){Io::revoke(jobs[i]);QTRY_VERIFY_WITH_TIMEOUT(Io::released(jobs[i]),3000);io.release(jobs[i]);}
    QTRY_COMPARE_WITH_TIMEOUT(io.queued(),std::size_t{0},3000);
    for(int round=0;round<16;++round){auto accepted=io.submit([v=*versions](std::stop_token)->Io::Result{auto valid=v->validate();if(!valid){return std::unexpected(valid.error());}return Io::Data{};});QVERIFY(accepted);QTRY_VERIFY_WITH_TIMEOUT(phase(*accepted,Io::Phase::Ready),3000);auto result=Io::take(*accepted);QVERIFY(result&&*result);Io::acknowledge(*accepted);QTRY_VERIFY_WITH_TIMEOUT(Io::released(*accepted),3000);io.release(*accepted);QVERIFY(!Io::take(jobs[0]));}
    qInfo("Profile IO high-water=%zu (queued+active+ready),16 later real validation rounds reclaimed",io.highWater());io.requestStop();io.join();
  }
};
QTEST_GUILESS_MAIN(AzureusProfileIoTests)
#include "azureus_profile_io_tests.moc"
