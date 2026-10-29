#include "persistent_session.hpp"
#include "managed_storage.hpp"
#include "library_commands.hpp"
#include "file_policy_commands.hpp"
#include "owned_engine_fixture.hpp"
#include "verified_seeding_fixture.hpp"
#include "managed_queue_observations.hpp"
#include "managed_tracker_responses.hpp"
#include "queued_mainline_scrape.hpp"
#include <QNetworkDatagram>
#include "managed_payload_counters.hpp"
#include <libtorrent/alert_types.hpp>
#include <libtorrent/hasher.hpp>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
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
// Owned HTTP tracker for the existing exact provider observer. Only actual
// loopback HTTP requests matching the fixture's20-byte info hash are answered.
class RawScrapeTracker final {
  QTcpServer server_;
  std::string hash_;
  void fail(const char* message){if(failure.isEmpty()){failure=QString::fromLatin1(message);}}
  void serve(QTcpSocket* socket,QByteArray request){
    const auto line=request.left(request.indexOf("\r\n"));const auto fields=line.split(' ');
    if(fields.size()!=3||fields[0]!="GET"){fail("Invalid owned tracker HTTP request");socket->abort();return;}
    const auto target=fields[1];const auto begin=target.indexOf("info_hash=");
    if(begin<0){fail("Owned tracker request has no info hash");socket->abort();return;}
    auto escaped=target.mid(begin+10);const auto end=escaped.indexOf('&');if(end>=0){escaped.truncate(end);}
    if(QByteArray::fromPercentEncoding(escaped)!=QByteArray::fromStdString(hash_)){fail("Owned tracker received wrong selected hash");socket->abort();return;}
    QByteArray body;bool failed=false;
    if(target.startsWith("/announce?")){++announces;body=announceCounts?"d8:completei5e10:incompletei6e8:intervali3600e5:peers0:e":"d8:intervali3600e5:peers0:e";}
    else if(target.startsWith("/scrape?")){
      ++scrapes;failed=errorReply;
      if(failed){body="owned scrape failure";}
      else{libtorrent::entry data;data["files"][hash_]["complete"]=3;data["files"][hash_]["incomplete"]=4;body=QByteArray::fromStdString(test::encoded(data));}
    }else{fail("Owned tracker received unexpected path");socket->abort();return;}
    const auto response=QByteArray(failed?"HTTP/1.1 500 Owned failure\r\n":"HTTP/1.1 200 OK\r\n")+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    if(socket->write(response)!=response.size()){fail("Owned tracker response write failed");}socket->disconnectFromHost();
  }
public:
  int announces=0,scrapes=0;bool errorReply=false,announceCounts=false;QString failure;
  explicit RawScrapeTracker(std::string hash):hash_(std::move(hash)){
    test::require(server_.listen(QHostAddress::LocalHost,0),"Cannot listen owned scrape tracker");
    QObject::connect(&server_,&QTcpServer::newConnection,&server_,[this]{
      while(auto* socket=server_.nextPendingConnection()){
        if(!socket->peerAddress().isLoopback()){fail("Owned tracker has nonloopback source");socket->abort();continue;}
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        auto input=std::make_shared<std::pair<QByteArray,bool>>();
        const auto receive=[this,socket,input]{
          auto& pending=input->first;auto& complete=input->second;
          pending+=socket->readAll();if(complete){if(!pending.isEmpty()){fail("Unexpected owned tracker pipelining");}return;}
          if(pending.size()>8192){fail("Oversize owned tracker request");socket->abort();return;}
          if(pending.contains("\r\n\r\n")){complete=true;serve(socket,pending);pending.clear();}
        };
        QObject::connect(socket,&QTcpSocket::readyRead,socket,receive);
        if(socket->bytesAvailable()){receive();}
      }
    });
  }
  std::string url()const{return "http://127.0.0.1:"+std::to_string(server_.serverPort())+"/announce";}
};
// Two owned numeric UDP trackers. The first actual announce is held and
// explicitly failed; only the other endpoint can succeed. Any SCRAPE exposes
// an accidental new producer and fails, independent of count assertions.
class ActiveUdpPair final {
  std::array<std::unique_ptr<QUdpSocket>,2> sockets;
  struct Held {QByteArray request;QHostAddress host;quint16 port=0;int index=-1;};
  std::optional<Held> pending;
  static std::uint32_t word(const QByteArray& p,int at){std::uint32_t v=0;for(int i=0;i<4;++i){v=(v<<8)|static_cast<unsigned char>(p[at+i]);}return v;}
  static std::uint64_t wide(const QByteArray& p,int at){return (static_cast<std::uint64_t>(word(p,at))<<32)|word(p,at+4);}
  static QByteArray number(std::uint32_t v){QByteArray p(4,'\0');for(int i=3;i>=0;--i){p[i]=static_cast<char>(v&255);v>>=8;}return p;}
  void send(int index,const QHostAddress& host,quint16 port,const QByteArray& data){if(sockets[static_cast<std::size_t>(index)]->writeDatagram(data,host,port)!=data.size()){failure="Owned active UDP reply failed";}}
  void receive(int index){auto& socket=*sockets[static_cast<std::size_t>(index)];while(socket.hasPendingDatagrams()){
    const auto d=socket.receiveDatagram();const auto p=d.data();const auto port=d.senderPort();
    if(!d.senderAddress().isLoopback()||port<1||port>65535||p.size()<16){failure="Owned active UDP source/shape";continue;}const auto sourcePort=static_cast<quint16>(port);const auto action=word(p,8);
    if(action==0){if(p.size()!=16||wide(p,0)!=0x41727101980ULL){failure="Owned active UDP CONNECT";continue;}++connects;send(index,d.senderAddress(),sourcePort,number(0)+p.mid(12,4)+number(0x11335577)+number(0x99aabbcc));continue;}
    if(action==2){++scrapes;failure="Active count selection emitted a forbidden SCRAPE";continue;}
    // The exact provider appends BEP41 URLData for /announce. Validate that
    // extension too; the fixed BEP15 header alone is not the full packet.
    const QByteArray path="/announce";const auto options=QByteArray(1,char(2))+QByteArray(1,static_cast<char>(path.size()))+path;
    if(action!=1||p.size()!=98+options.size()||p.mid(98)!=options||wide(p,0)!=0x1133557799aabbccULL||p.mid(16,20)!=expectedHash){failure=QString("Owned active UDP announce identity/options: action=%1 bytes=%2 expected=%3").arg(action).arg(p.size()).arg(98+options.size());continue;}
    const auto advertised=(static_cast<unsigned char>(p[96])<<8)|static_cast<unsigned char>(p[97]);if(advertised!=peerPort){failure="Owned active UDP advertised peer port";continue;}
    if(word(p,80)==3){++stops;send(index,d.senderAddress(),sourcePort,number(1)+p.mid(12,4)+number(3600)+number(8)+number(7));continue;}
    ++announces;
    if(first<0){first=index;pending=Held{p,d.senderAddress(),sourcePort,index};continue;}
    if(index==first){failure="Owned failed endpoint was reused before the other active URL";continue;}
    second=index;send(index,d.senderAddress(),sourcePort,number(1)+p.mid(12,4)+number(3600)+number(8)+number(7));
  }}
public:
  QByteArray expectedHash;int peerPort=0,first=-1,second=-1,connects=0,announces=0,scrapes=0,stops=0;QString failure;
  ActiveUdpPair(){for(int i=0;i<2;++i){auto& socket=sockets[static_cast<std::size_t>(i)];socket=std::make_unique<QUdpSocket>();test::require(socket->bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Cannot bind owned active UDP tracker");QObject::connect(socket.get(),&QUdpSocket::readyRead,socket.get(),[this,i]{receive(i);});}}
  std::string url(int i)const{return "udp://127.0.0.1:"+std::to_string(sockets[static_cast<std::size_t>(i)]->localPort())+"/announce";}
  bool held()const{return pending.has_value();}
  void failFirst(){if(!pending){return;}auto value=std::move(*pending);pending.reset();send(value.index,value.host,value.port,number(3)+value.request.mid(12,4)+"owned active failover");}
};
struct ActiveUdpCleanup {engine::PersistentSession& owner;ActiveUdpPair& trackers;~ActiveUdpCleanup(){trackers.failFirst();static_cast<void>(owner.close(std::chrono::steady_clock::now()+10s));}};
} // namespace

class ManagedQueueTests final:public QObject {
  Q_OBJECT
private slots:

  void ordinaryMmapLatencyUsesThisAdmissionAndActualPayloadCallbacks_data(){QTest::addColumn<bool>("multi");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;}
  void ordinaryMmapLatencyUsesThisAdmissionAndActualPayloadCallbacks(){
    QFETCH(bool,multi);test::OwnedEngineFixture fixture(multi);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto lease=held(fixture);auto service=owner();auto p=policy(1,2);p.rankMode=core::QueueRankMode::None;p.minimumSeedSeconds=0;p.minimumSeedActiveSeconds=0;p.ignoreZeroPeers=false;p.firstPriorityIgnoreZeroPeers=false;
    QVERIFY(service->configureQueuePolicy(p));QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port()}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&current->complete&&current->received>0&&current->diskReadLatencyMillis&&current->diskWriteLatencyMillis;})(),30000);
    const auto before=status(*service,*fixture.hash);QVERIFY(before);const auto admission=before->admission;
    test::VerifiedSeedCase actual{fixture.root.path(),fixture.profile,fixture.payload,fixture.stateFile,*fixture.hash,fixture.files};
    QVERIFY(!test::receiveVerifiedSeed(*service,actual).isEmpty());QVERIFY(service->poll());
    const auto uploaded=status(*service,*fixture.hash);QVERIFY(uploaded&&!uploaded->failure&&uploaded->sent>0&&uploaded->admission==admission);
    QVERIFY(uploaded->diskReadLatencyMillis&&uploaded->diskWriteLatencyMillis);
    QVERIFY(uploaded->queueIdleUploadSeconds&&*uploaded->queueIdleUploadSeconds>=0);
    QVERIFY(*uploaded->diskReadLatencyMillis>=0&&*uploaded->diskWriteLatencyMillis>=0);
    QVERIFY(service->stopManaged(*fixture.hash));const auto stopping=status(*service,*fixture.hash);QVERIFY(stopping&&!stopping->diskReadLatencyMillis&&!stopping->diskWriteLatencyMillis&&!stopping->queueIdleUploadSeconds);
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&!current->retained;})(),10000);
    const auto retired=status(*service,*fixture.hash);QVERIFY(retired&&!retired->diskReadLatencyMillis&&!retired->diskWriteLatencyMillis);
    const auto checkpointed=parsed(test::read(fixture.stateFile));const auto* attributes=checkpointed.find("attributes");QVERIFY(attributes);
    const auto* savedIdle=attributes->find("timesinceul");QVERIFY(savedIdle&&savedIdle->type==core::Node::Type::Integer&&savedIdle->integer>=0);
    QVERIFY(service->startManaged(lease,{.hash=*fixture.hash}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto current=status(*service,*fixture.hash);return poll&&current&&!current->failure&&current->admission>admission&&current->complete&&current->diskReadLatencyMillis&&current->diskWriteLatencyMillis;})(),10000);
    const auto fresh=status(*service,*fixture.hash);QVERIFY(fresh);QCOMPARE(fresh->sent,std::int64_t{0});QCOMPARE(*fresh->diskWriteLatencyMillis,std::int64_t{0});QVERIFY(fresh->queueIdleUploadSeconds);QVERIFY(*fresh->queueIdleUploadSeconds>=savedIdle->integer);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(fixture.intact());
  }

  void rawScrapeTerminalUsesSameAdmissionRingAndWithdrawsOnLoss(){
    std::array<unsigned char,32> key{};key[0]=3;
    engine::detail::ManagedQueueObservations observations(1);
    observations.trackerOrder(std::vector<std::array<unsigned char,32>>{key});observations.restore(std::optional<std::string_view>{"3,9,8"},{});observations.installed();
    QVERIFY(!observations.facts(1000).lastTrackerScrape);
    std::thread producer([&]{core::QueueObservation e;e.kind=core::QueueObservationKind::ScrapeFailed;e.sequence=1;e.request=4;
      e.observedNanos=2000000000;e.requestStartedNanos=1900000000;e.observedWallMillis=200000;e.requestStartedWallMillis=199900;e.protocol=1;e.tracker=key;observations.observe(e);});
    producer.join();auto fact=observations.facts(2000);QVERIFY(fact.lastTrackerScrape);QCOMPARE(fact.lastTrackerScrape->request,std::uint64_t{4});
    std::atomic<bool> foreign{false};std::thread reader([&]{foreign=observations.facts(2000).lastTrackerScrape.has_value();});reader.join();QVERIFY(!foreign.load());QVERIFY(observations.facts(2000).lastTrackerScrape);
    std::thread overflow([&]{core::QueueObservation e;e.kind=core::QueueObservationKind::ScrapeFailed;e.request=5;e.observedNanos=3000000000;e.requestStartedNanos=2900000000;e.protocol=1;e.tracker=key;
      for(std::uint64_t i=0;i<=engine::detail::ManagedQueueObservations::Capacity;++i){e.sequence=i+2;observations.observe(e);}});
    overflow.join();QVERIFY(!observations.facts(3000).lastTrackerScrape);
    engine::detail::ManagedQueueObservations next(1);next.trackerOrder(std::vector<std::array<unsigned char,32>>{key});next.installed();QVERIFY(!next.facts(4000).lastTrackerScrape);
  }
  void actualOwnedTrackerScrapeReplyAndErrorReachRetainedRawSource(){
    namespace lt=libtorrent;test::OwnedEngineFixture fixture(false);RawScrapeTracker tracker(hashBytes(*fixture.hash));
    const auto url=tracker.url();const auto digest=lt::hasher256(lt::span<const char>(url.data(),url.size())).final();std::array<unsigned char,32> key{};std::copy(digest.begin(),digest.end(),key.begin());
    auto observations=std::make_shared<engine::detail::ManagedQueueObservations>(1);observations->trackerOrder(std::vector<std::array<unsigned char,32>>{key});
    // This generated loopback tracker is explicitly owned; stock SSRF permits only /announce locally.
    auto settings=test::localSettings();settings.set_bool(lt::settings_pack::ssrf_mitigation,false);settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::tracker|lt::alert_category::status|lt::alert_category::error));
    auto session=std::make_unique<lt::session>(settings);const auto encodedMetadata=test::read(fixture.stateFile);lt::error_code error;
    lt::add_torrent_params parameters;parameters.ti=std::make_shared<lt::torrent_info>(lt::span<const char>(encodedMetadata.constData(),encodedMetadata.size()),error,lt::from_span);QVERIFY(!error);
    parameters.ti->clear_trackers();parameters.trackers={url};parameters.save_path=fixture.payload.toStdString();parameters.flags|=lt::torrent_flags::paused|lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;parameters.flags&=~lt::torrent_flags::auto_managed;
    const auto bridge=engine::detail::managedPayloadCounterBridge();QVERIFY(bridge);auto counters=std::make_shared<engine::detail::ManagedPayloadCounters>();
    QVERIFY(engine::detail::attachManagedPayloadCounters(parameters,counters,bridge,observations));auto handle=session->add_torrent(std::move(parameters),error);QVERIFY(!error&&handle.is_valid());handle.resume();
    bool announced=false;const auto pump=[&]{std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);for(const auto* a:alerts){const auto* reply=lt::alert_cast<lt::tracker_reply_alert>(a);if(reply&&reply->handle==handle){announced=true;}}return announced||!tracker.failure.isEmpty();};
    const auto now=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QCOMPARE(tracker.announces,1);QVERIFY(!observations->facts(now()).lastTrackerScrape);
    handle.scrape_tracker();std::optional<core::QueueScrapeTerminal> first;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{first=observations->facts(now()).lastTrackerScrape;return first.has_value()||!tracker.failure.isEmpty();})(),15000);
    QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QVERIFY(first);QCOMPARE(tracker.scrapes,1);QCOMPARE(first->kind,core::QueueScrapeTerminalKind::Reply);QCOMPARE(first->tracker,key);QCOMPARE(first->protocol,1u);QCOMPARE(first->seeds,3);QCOMPARE(first->peers,4);QVERIFY(first->request);QVERIFY(first->requestStartedNanos>0&&first->requestStartedNanos<=first->observedNanos);
    tracker.errorReply=true;handle.scrape_tracker();std::optional<core::QueueScrapeTerminal> failed;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{failed=observations->facts(now()).lastTrackerScrape;return (failed&&failed->sequence>first->sequence)||!tracker.failure.isEmpty();})(),15000);
    QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QVERIFY(failed);QCOMPARE(tracker.scrapes,2);QCOMPARE(failed->kind,core::QueueScrapeTerminalKind::RequestFailed);QCOMPARE(failed->tracker,key);QVERIFY(failed->request>first->request);QCOMPARE(failed->seeds,-1);QCOMPARE(failed->peers,-1);
    const auto historical=observations->facts(now());QVERIFY(historical.swarm&&historical.swarm->success); // Error RPC is not original RT_ERROR authority.
    session.reset();QVERIFY(fixture.intact(false));
  }
  void actualAnnounceResponseSurvivesScrapeErrorWithoutGrantingConfiguredPermission(){
    namespace lt=libtorrent;test::OwnedEngineFixture fixture(false);RawScrapeTracker tracker(hashBytes(*fixture.hash));tracker.announceCounts=true;
    const auto url=tracker.url();const auto digest=lt::hasher256(lt::span<const char>(url.data(),url.size())).final();std::array<unsigned char,32> key{};std::copy(digest.begin(),digest.end(),key.begin());
    auto observations=std::make_shared<engine::detail::ManagedQueueObservations>(1);observations->trackerOrder(std::vector<std::array<unsigned char,32>>{key});
    // This generated loopback tracker is explicitly owned; stock SSRF permits only /announce locally.
    auto settings=test::localSettings();settings.set_bool(lt::settings_pack::ssrf_mitigation,false);settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::tracker|lt::alert_category::status|lt::alert_category::error));
    auto session=std::make_unique<lt::session>(settings);const auto encodedMetadata=test::read(fixture.stateFile);lt::error_code error;
    lt::add_torrent_params parameters;parameters.ti=std::make_shared<lt::torrent_info>(lt::span<const char>(encodedMetadata.constData(),encodedMetadata.size()),error,lt::from_span);QVERIFY(!error);
    parameters.ti->clear_trackers();parameters.trackers={url};parameters.save_path=fixture.payload.toStdString();parameters.flags|=lt::torrent_flags::paused|lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;parameters.flags&=~lt::torrent_flags::auto_managed;
    const auto bridge=engine::detail::managedPayloadCounterBridge();QVERIFY(bridge);auto counters=std::make_shared<engine::detail::ManagedPayloadCounters>();
    QVERIFY(engine::detail::attachManagedPayloadCounters(parameters,counters,bridge,observations));auto handle=session->add_torrent(std::move(parameters),error);QVERIFY(!error&&handle.is_valid());handle.resume();
    bool announced=false;const auto pump=[&]{std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);for(const auto* a:alerts){const auto* reply=lt::alert_cast<lt::tracker_reply_alert>(a);if(reply&&reply->handle==handle){announced=true;}}return announced||!tracker.failure.isEmpty();};
    const auto now=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QCOMPARE(tracker.announces,1);QVERIFY(!observations->facts(now()).lastTrackerScrape);
    const auto announcedResponse=observations->facts(now()).singleTrackerResponse;QVERIFY(announcedResponse);QCOMPARE(announcedResponse->response.type,core::QueueOriginalScrapeType::Success);QVERIFY(announcedResponse->response.content);QCOMPARE(announcedResponse->response.content->seeds,5);QCOMPARE(announcedResponse->response.content->peers,6);QVERIFY(!core::sourceQueueQueuedScrapeEligible(announcedResponse->response,{}));
    tracker.errorReply=true;handle.scrape_tracker();std::optional<core::QueueScrapeTerminal> first;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{first=observations->facts(now()).lastTrackerScrape;return first.has_value()||!tracker.failure.isEmpty();})(),15000);
    QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QVERIFY(first);QCOMPARE(tracker.scrapes,1);QCOMPARE(first->kind,core::QueueScrapeTerminalKind::RequestFailed);QCOMPARE(first->tracker,key);QCOMPARE(first->protocol,1u);QCOMPARE(first->seeds,-1);QCOMPARE(first->peers,-1);QVERIFY(first->request);QVERIFY(first->requestStartedNanos>0&&first->requestStartedNanos<=first->observedNanos);
    const auto retained=observations->facts(now()).singleTrackerResponse;QVERIFY(retained);QCOMPARE(retained->response.type,core::QueueOriginalScrapeType::Success);QVERIFY(retained->response.content);QCOMPARE(retained->response.content->startedWallMillis,announcedResponse->response.content->startedWallMillis);QCOMPARE(retained->response.content->seeds,5);
    tracker.errorReply=false;handle.scrape_tracker();std::optional<core::QueueScrapeTerminal> failed;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{failed=observations->facts(now()).lastTrackerScrape;return (failed&&failed->sequence>first->sequence)||!tracker.failure.isEmpty();})(),15000);
    QVERIFY2(tracker.failure.isEmpty(),qPrintable(tracker.failure));QVERIFY(failed);QCOMPARE(tracker.scrapes,2);QCOMPARE(failed->kind,core::QueueScrapeTerminalKind::Reply);QCOMPARE(failed->tracker,key);QVERIFY(failed->request>first->request);QCOMPARE(failed->seeds,3);QCOMPARE(failed->peers,4);
    const auto historical=observations->facts(now());QVERIFY(historical.singleTrackerResponse);QCOMPARE(historical.singleTrackerResponse->response.type,core::QueueOriginalScrapeType::Success);QVERIFY(!historical.singleTrackerResponse->response.content);QVERIFY(!core::sourceQueueQueuedScrapeEligible(historical.singleTrackerResponse->response,{})); // ABI omits downloaded validity: exact scrape content remains unavailable.
    session.reset();QVERIFY(fixture.intact(false));
  }
  void activeUrlInventoryIsBoundedDeduplicatedAndNeverInventsASelection(){
    libtorrent::add_torrent_params params;params.trackers={"http://127.0.0.1:1/announce","udp://127.0.0.1:2/announce","http://127.0.0.1:1/announce"};
    const auto urls=engine::detail::effectiveTrackerResponseUrls(params);QVERIFY(urls);QCOMPARE(urls->size(),std::size_t{2});QVERIFY(!engine::detail::singleEffectiveTracker(params));
    params.trackers.push_back("wss://127.0.0.1:3/announce");QVERIFY(!engine::detail::effectiveTrackerResponseUrls(params));params.trackers.clear();
    for(int i=0;i<8;++i){params.trackers.push_back("http://127.0.0.1:"+std::to_string(i+1)+"/announce");}QVERIFY(engine::detail::effectiveTrackerResponseUrls(params));
    params.trackers.push_back("http://127.0.0.1:9/announce");QVERIFY(!engine::detail::effectiveTrackerResponseUrls(params));
    params.trackers={"udp://127.0.0.1:1/announce","udp://127.0.0.1:2/announce"};params.tracker_tiers={0,0};QVERIFY(engine::detail::singleTierUdpResponseUrls(params));params.tracker_tiers={0,1};QVERIFY(!engine::detail::singleTierUdpResponseUrls(params));
  }
  void perUrlActiveRingKeepsCountsSeparateAndLossWithdrawsEveryBinding(){
    using Ring=engine::detail::ManagedTrackerResponse;
    auto a=std::make_shared<core::QueueOriginalTrackerResponse>(),b=std::make_shared<core::QueueOriginalTrackerResponse>();
    std::array<unsigned char,32> ad{},bd{};ad[0]=1;bd[0]=2;
    const std::vector<Ring::Binding> bindings{{"http://127.0.0.1:1/announce","a",ad,a,{}},{"udp://127.0.0.1:2/announce","b",bd,b,{}}};
    Ring ring(71,1,bindings);ring.installed=true;QVERIFY(!ring.sole());QVERIFY(!ring.latestTerminal());QVERIFY(ring.urlTerminals().empty());
    Ring::Value value;value.admission=71;value.sequence=1;value.request=1;value.protocol=1;value.tracker=ad;value.requestStartedNanos=1;value.observedNanos=1000000000;value.terminal.kind=core::QueueTrackerTerminalKind::Announce;value.terminal.comparable=true;value.terminal.present=3;value.terminal.complete=5;value.terminal.incomplete=6;value.terminal.observedMillis=1000;value.terminal.observedWallMillis=10000;
    ring.observe(value);QVERIFY(ring.drain());auto rows=ring.urlTerminals();QCOMPARE(rows.size(),std::size_t{1});QCOMPARE(rows[0].url,bindings[0].url);QCOMPARE(rows[0].response.content->seeds,5);QVERIFY(!b->response().content);
    value.sequence=2;value.request=2;value.tracker=bd;value.observedNanos=2000000000;value.terminal.observedMillis=2000;value.terminal.observedWallMillis=20000;value.terminal.complete=7;value.terminal.incomplete=8;ring.observe(value);QVERIFY(ring.drain());
    rows=ring.urlTerminals();QCOMPARE(rows.size(),std::size_t{2});QCOMPARE(rows[0].response.content->seeds,5);QCOMPARE(rows[1].response.content->seeds,7);QVERIFY(!ring.latestTerminal());
    // An ignored failed announce neither replaces that URL's response nor
    // supplies a selected-active URL or a newer scrape timestamp.
    value.sequence=3;value.request=3;value.observedNanos=3000000000;value.terminal.kind=core::QueueTrackerTerminalKind::AnnounceFailed;value.terminal.comparable=false;ring.observe(value);QVERIFY(ring.drain());
    rows=ring.urlTerminals();QCOMPARE(rows[1].terminal.sequence,std::uint64_t{2});QCOMPARE(rows[1].response.content->seeds,7);
    // Actual retirement can replace the ring without resetting shared URL
    // history. A new admission still has no fabricated terminal observation.
    Ring next(72,1,bindings);next.installed=true;QVERIFY(next.urlTerminals().empty());QCOMPARE(b->response().content->seeds,7);
    std::vector<Ring::UrlTerminal> foreign;std::thread reader([&]{foreign=ring.urlTerminals();});reader.join();QVERIFY(foreign.empty());QCOMPARE(ring.urlTerminals().size(),std::size_t{2});
    value.sequence=4;value.request=4;value.tracker[0]=9;value.observedNanos=4000000000;ring.observe(value);QVERIFY(!ring.drain());QVERIFY(ring.urlTerminals().empty());QCOMPARE(a->response().type,core::QueueOriginalScrapeType::Unknown);QCOMPARE(b->response().type,core::QueueOriginalScrapeType::Unknown);
    auto c=std::make_shared<core::QueueOriginalTrackerResponse>(),d=std::make_shared<core::QueueOriginalTrackerResponse>();auto bounded=bindings;bounded[0].response=c;bounded[1].response=d;Ring overflow(73,1,bounded);overflow.installed=true;
    for(std::uint64_t i=0;i<=Ring::Capacity;++i){value.admission=73;value.sequence=i+1;value.request=i+1;value.tracker=ad;value.observedNanos=static_cast<std::int64_t>(i+1);overflow.observe(value);}QVERIFY(!overflow.drain());QVERIFY(overflow.urlTerminals().empty());
    Ring stale(74,1,bounded);stale.installed=true;value.admission=73;value.sequence=1;stale.observe(value);QVERIFY(!stale.drain());QVERIFY(stale.urlTerminals().empty());
  }
  void actualMultiUrlProviderRoutesEachTerminalWithoutSelectingAnAnnouncer(){
    namespace lt=libtorrent;test::OwnedEngineFixture fixture(false);auto lease=held(fixture);
    RawScrapeTracker first(hashBytes(*fixture.hash)),second(hashBytes(*fixture.hash));first.announceCounts=true;
    auto files=engine::detail::ManagedStorage::prepare(lease,*fixture.hash,engine::NetworkPolicy{});QVERIFY(files);
    auto settings=test::localSettings();settings.set_bool(lt::settings_pack::ssrf_mitigation,false); // Exact owned local /scrape only.
    settings.set_bool(lt::settings_pack::announce_to_all_tiers,true);settings.set_bool(lt::settings_pack::announce_to_all_trackers,true);
    auto session=std::make_unique<lt::session>(settings);const auto encodedMetadata=test::read(fixture.stateFile);lt::error_code error;
    lt::add_torrent_params params;params.ti=std::make_shared<lt::torrent_info>(lt::span<const char>(encodedMetadata.constData(),encodedMetadata.size()),error,lt::from_span);QVERIFY(!error);
    params.ti->clear_trackers();params.trackers={first.url(),second.url()};params.tracker_tiers={0,1};params.save_path=fixture.payload.toStdString();params.flags|=lt::torrent_flags::paused|lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;params.flags&=~lt::torrent_flags::auto_managed;
    engine::detail::LibraryTrackerResponses library;auto ring=engine::detail::attachManagedTrackerResponse(params,lease,files->identity(),81,library);QVERIFY(ring);QVERIFY(!ring->sole());QCOMPARE(library.size(),std::size_t{2});
    auto handle=session->add_torrent(std::move(params),error);QVERIFY(!error&&handle.is_valid());handle.resume();
    const auto pump=[&]{std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);return ring->drain()&&first.failure.isEmpty()&&second.failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return !first.failure.isEmpty()||!second.failure.isEmpty()||ring->lost.load();}return first.announces==1&&second.announces==1&&ring->urlTerminals().size()==1;})(),15000);
    QVERIFY2(first.failure.isEmpty(),qPrintable(first.failure));QVERIFY2(second.failure.isEmpty(),qPrintable(second.failure));QVERIFY(!ring->lost.load());auto rows=ring->urlTerminals();QCOMPARE(rows.size(),std::size_t{1});QCOMPARE(rows[0].url,first.url());QCOMPARE(rows[0].response.content->seeds,5);QCOMPARE(rows[0].response.content->peers,6);QVERIFY(!ring->latestTerminal());QCOMPARE(first.scrapes,0);QCOMPARE(second.scrapes,0);
    const auto trackers=handle.trackers();const auto at=std::ranges::find(trackers,second.url(),&lt::announce_entry::url);QVERIFY(at!=trackers.end());const auto index=std::distance(trackers.begin(),at);QVERIFY(index>=0&&index<8);
    handle.scrape_tracker(static_cast<int>(index)); // Test-only actual RPC; production never issues one here.
    QTRY_VERIFY_WITH_TIMEOUT(([&]{return pump()&&second.scrapes==1&&ring->urlTerminals().size()==2;})(),15000);
    rows=ring->urlTerminals();const auto a=std::ranges::find(rows,first.url(),&engine::detail::ManagedTrackerResponse::UrlTerminal::url),b=std::ranges::find(rows,second.url(),&engine::detail::ManagedTrackerResponse::UrlTerminal::url);
    QVERIFY(a!=rows.end()&&b!=rows.end());QVERIFY(a->response.content&&b->response.content);QCOMPARE(a->response.content->seeds,5);QCOMPARE(b->response.content->seeds,3);QCOMPARE(b->response.content->peers,4);QVERIFY(a->terminal.sequence!=b->terminal.sequence);QVERIFY(!ring->latestTerminal());
    session.reset();QVERIFY(fixture.intact(false));QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());
  }
  void realManagedOwnerExposesUrlFactsButNeverClaimsAMultiUrlBestResponse_data(){
    QTest::addColumn<bool>("dhtEnabled");QTest::newRow("no-DHT-service")<<false;QTest::newRow("live-DHT-no-active-selection")<<true;
  }
  void realManagedOwnerExposesUrlFactsButNeverClaimsAMultiUrlBestResponse(){
    QFETCH(bool,dhtEnabled);
    test::OwnedEngineFixture fixture(false);RawScrapeTracker first(hashBytes(*fixture.hash)),second(hashBytes(*fixture.hash));first.announceCounts=true;second.announceCounts=true;
    auto source=parsed(test::read(fixture.stateFile));QVERIFY(source.replaceValue("announce",test::encoded(libtorrent::entry(first.url()))));
    const libtorrent::entry::list_type tiers{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(first.url())}),libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(second.url())})};
    QVERIFY(source.replaceValue("announce-list",test::encoded(libtorrent::entry(tiers))));
    auto attrs=parsed(QByteArray::fromStdString(encoded(*source.find("attributes"))));QVERIFY(attrs.replaceValue("pluginoptions","d5:mldhtd14:enableannouncei0eee"));QVERIFY(source.replaceValue("attributes",attrs.serialize()));
    const auto raw=source.find("info");QVERIFY(raw);const auto rawInfo=encoded(*raw);test::write(fixture.stateFile,QByteArray::fromStdString(source.serialize()));
    auto lease=held(fixture);auto service=owner();engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.httpTrackers=true;p.dht=dhtEnabled;p.mainlineIpv4=true;p.mainlineIpv6=false;p.mainlinePeerBootstrapOnly=true;p.bindAddresses={"127.0.0.1"};QUdpSocket discovery;QVERIFY(discovery.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));p.mainlinePort=discovery.localPort();discovery.close();QTcpServer reserved;QVERIFY(reserved.listen(QHostAddress::LocalHost,0));p.listenPort=reserved.serverPort();reserved.close();
    QVERIFY(service->configureNetwork(p));QVERIFY(service->configureMainlineBootstrap({}));QVERIFY(service->startIdle(lease));QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.queued=false}));
    const auto pump=[&]{const auto result=service->poll();const auto rows=service->managedDownloads();return result&&rows.size()==1&&!rows[0].failure&&first.failure.isEmpty()&&second.failure.isEmpty();};
    std::vector<engine::ManagedTrackerUrlResponse> facts;std::uint64_t admission=0;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return false;}const auto rows=service->managedDownloads();admission=rows[0].admission;facts=service->managedTrackerUrlResponses(*fixture.hash,admission);return rows[0].checked&&!facts.empty();})(),15000);
    QVERIFY(admission);QVERIFY(!facts.empty());for(const auto& row:facts){QVERIFY(row.url==first.url()||row.url==second.url());QVERIFY(row.response.content);QCOMPARE(row.response.content->seeds,5);QCOMPARE(row.response.content->peers,6);QVERIFY(row.sequence&&row.observedWallMillis>0);}
    QVERIFY(!service->managedTrackerResponse(*fixture.hash,admission));QVERIFY(service->managedTrackerUrlResponses(*fixture.hash,admission+1).empty());QVERIFY(service->libraryTrackerResponses().empty());QCOMPARE(first.scrapes,0);QCOMPARE(second.scrapes,0);QVERIFY(service->mainlineScrapeEstimates().empty());
    if(dhtEnabled){QTRY_VERIFY_WITH_TIMEOUT(pump()&&service->mainlineDiscovery().state==engine::MainlineDiscoveryStatus::State::Running,15000);}
    auto discoveryStatus=service->mainlineDiscovery();QVERIFY(discoveryStatus.admittedScrapeCandidates.empty());QCOMPARE(discoveryStatus.lookups,std::uint64_t{0});QCOMPARE(discoveryStatus.announces,std::uint64_t{0});QCOMPARE(discoveryStatus.deliveredPeers,std::uint64_t{0});
    // A saved edit to one URL is not a new sole-response admission. Public
    // rearm forces fresh owner reconciliation without inventing a clock hook.
    auto changed=parsed(test::read(fixture.stateFile));QVERIFY(changed.replaceValue("announce-list",test::encoded(libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(first.url())})}))));test::write(fixture.stateFile,QByteArray::fromStdString(changed.serialize()));
    const auto externalState=test::read(fixture.stateFile);QVERIFY(service->rearmMainlineScrape(*fixture.hash));QVERIFY(pump());QVERIFY(!service->managedTrackerResponse(*fixture.hash,admission));QVERIFY(service->libraryTrackerResponses().empty());QVERIFY(service->mainlineScrapeEstimates().empty());
    discoveryStatus=service->mainlineDiscovery();QVERIFY(discoveryStatus.admittedScrapeCandidates.empty());QCOMPARE(discoveryStatus.lookups,std::uint64_t{0});QCOMPARE(discoveryStatus.announces,std::uint64_t{0});QCOMPARE(discoveryStatus.deliveredPeers,std::uint64_t{0});QCOMPARE(first.scrapes,0);QCOMPARE(second.scrapes,0);
    QVERIFY(service->pauseManaged(*fixture.hash,true));QVERIFY(service->managedTrackerUrlResponses(*fixture.hash,admission).empty());
    const auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    // A deliberate edit to the retained .dat is a real checkpoint CAS
    // conflict, not permission to replace the external tracker edit.
    QVERIFY(closed.checkpointFailure);QCOMPARE(closed.checkpointFailure->code,storage::ErrorCode::Conflict);QCOMPARE(closed.checkpointFailure->message,std::string("Saved state changed since loading"));QCOMPARE(test::read(fixture.stateFile),externalState);QVERIFY(service->managedTrackerUrlResponses(*fixture.hash,admission).empty());
    const auto after=parsed(test::read(fixture.stateFile));QCOMPARE(encoded(*after.find("info")),rawInfo);QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());QVERIFY(fixture.accountingIntact(false));
  }
  void singleHelperDispatchCutOrdersResponseAndAlertDeliveryWithoutClockTranslation(){
    using Ring=engine::detail::ManagedTrackerResponse;std::array<unsigned char,32> a{},b{};a[0]=1;b[0]=2;
    auto first=std::make_shared<core::QueueOriginalTrackerResponse>(),second=std::make_shared<core::QueueOriginalTrackerResponse>();
    Ring ring(91,1,std::vector<Ring::Binding>{{"udp://127.0.0.1:1/announce","a",a,first,{}},{"udp://127.0.0.1:2/announce","b",b,second,{}}});ring.installed=true;
    Ring::Value value;value.admission=91;value.protocol=1;value.sequence=1;value.request=1;value.requestStartedNanos=1;value.observedNanos=1000000000;value.tracker=a;value.terminal.kind=core::QueueTrackerTerminalKind::Announce;value.terminal.present=3;value.terminal.comparable=true;value.terminal.complete=5;value.terminal.incomplete=6;value.terminal.observedMillis=1000;
    // Terminal already written at the cut: the actual dispatch is delivered
    // in the same collection before drain. Only that explicit URL is selected.
    ring.observe(value);ring.beginDispatchCollection();ring.observeUdpDispatch("udp://127.0.0.1:1/announce",1,100);QVERIFY(ring.drain());auto selected=ring.currentUdpTerminal();QVERIFY(selected&&selected->response.content);QCOMPARE(selected->url,std::string("udp://127.0.0.1:1/announce"));QCOMPARE(selected->response.content->seeds,5);
    // A callback arriving AFTER collection began cannot select even when its
    // newer response is drained. The next collection supplies its dispatch.
    ring.beginDispatchCollection();value.sequence=2;value.request=2;value.tracker=b;value.observedNanos=2000000000;value.terminal.complete=7;value.terminal.incomplete=8;value.terminal.observedMillis=2000;ring.observe(value);QVERIFY(ring.drain());QVERIFY(!ring.currentUdpTerminal());
    ring.beginDispatchCollection();ring.observeUdpDispatch("udp://127.0.0.1:2/announce",1,200);QVERIFY(ring.drain());selected=ring.currentUdpTerminal();QVERIFY(selected&&selected->response.content);QCOMPARE(selected->response.content->seeds,7);
    // A later terminal for another URL never changes the dispatch-selected
    // URL. Both histories survive, but current getTrackerURL remains explicit.
    value.sequence=3;value.request=3;value.tracker=a;value.observedNanos=3000000000;value.terminal.complete=9;value.terminal.observedMillis=3000;ring.observe(value);ring.beginDispatchCollection();QVERIFY(ring.drain());selected=ring.currentUdpTerminal();QVERIFY(selected&&selected->response.content);QCOMPARE(selected->url,std::string("udp://127.0.0.1:2/announce"));QCOMPARE(selected->response.content->seeds,7);
    ring.observeUdpDispatch("udp://127.0.0.1:2/announce",2,300);QVERIFY(ring.currentUdpTerminal()); // Unselected protocol is not this helper's URL.
    ring.observeUdpDispatch("udp://127.0.0.1:2/announce",1,199);QVERIFY(!ring.currentUdpTerminal()); // Actual alert clock regression withdraws until retirement.
    Ring unknown(92,1,std::vector<Ring::Binding>{{"udp://127.0.0.1:1/announce","a",a,first,{}},{"udp://127.0.0.1:2/announce","b",b,second,{}}});unknown.installed=true;unknown.observeUdpDispatch("udp://127.0.0.1:3/announce",1,100);QVERIFY(!unknown.currentUdpTerminal());
    ring.loseDispatch();ring.observeUdpDispatch("udp://127.0.0.1:2/announce",1,400);QVERIFY(!ring.currentUdpTerminal()); // Drop never silently regains selection.
  }
  void singleOriginalUdpTierCountsRequireAnActualCapturedAdmissionAndExactGroup(){
    test::OwnedEngineFixture fixture(false);const std::string a="udp://127.0.0.1:12345/announce",b="udp://127.0.0.1:12346/announce";
    auto source=parsed(test::read(fixture.stateFile));QVERIFY(source.replaceValue("announce",test::encoded(libtorrent::entry(a))));
    const auto tier=[](const std::string& one,const std::string& two){return test::encoded(libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(one),libtorrent::entry(two)})}));};
    QVERIFY(source.replaceValue("announce-list",tier(a,b)));test::write(fixture.stateFile,QByteArray::fromStdString(source.serialize()));auto lease=held(fixture);engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.udpTrackers=true;
    auto storage=engine::detail::ManagedStorage::prepare(lease,*fixture.hash,p);QVERIFY(storage);
    // Preparation publishes canonical mapping/resume sections. Capture that
    // actual accepted state before testing external tracker-only edits.
    const auto admittedState=test::read(fixture.stateFile);source=parsed(admittedState);
    const std::vector<engine::ManagedMainlineScrapeObservation> active{{&*storage,92,false}};
    const auto read=[&]{return engine::readCurrentMainlineScrapeTargets(lease,active,true,nullptr,engine::SavedTrackerPurpose::ActiveSingleUdpTier);};
    auto accepted=read();QVERIFY(accepted&&accepted->size()==1);QCOMPARE(accepted->front().admission,std::uint64_t{92});QCOMPARE(accepted->front().configuredTrackers.size(),std::size_t{2});
    auto noAdmission=engine::readCurrentMainlineScrapeTargets(lease,{},true,nullptr,engine::SavedTrackerPurpose::ActiveSingleUdpTier);QVERIFY(noAdmission&&noAdmission->empty());
    auto two=source;QVERIFY(two.replaceValue("announce-list",test::encoded(libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(a)}),libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(b)})}))));test::write(fixture.stateFile,QByteArray::fromStdString(two.serialize()));auto grouped=read();QVERIFY(grouped&&grouped->empty());
    auto different=source;QVERIFY(different.replaceValue("announce-list",tier(a,"udp://127.0.0.1:12347/announce")));test::write(fixture.stateFile,QByteArray::fromStdString(different.serialize()));auto rebound=read();QVERIFY(rebound&&rebound->empty());
    test::write(fixture.stateFile,admittedState);const auto restored=read();QVERIFY(restored&&restored->size()==1);
  }
  void actualSingleTierUdpFailoverPublishesCurrentCountsWithoutANewScrape_data(){QTest::addColumn<bool>("dhtEnabled");QTest::newRow("DHT-disabled")<<false;QTest::newRow("DHT-live-passive-only")<<true;}
  void actualSingleTierUdpFailoverPublishesCurrentCountsWithoutANewScrape(){
    QFETCH(bool,dhtEnabled);test::OwnedEngineFixture fixture(false);ActiveUdpPair trackers;trackers.expectedHash=QByteArray::fromStdString(hashBytes(*fixture.hash));
    auto source=parsed(test::read(fixture.stateFile));QVERIFY(source.replaceValue("announce",test::encoded(libtorrent::entry(trackers.url(0)))));
    const libtorrent::entry::list_type urls{libtorrent::entry(trackers.url(0)),libtorrent::entry(trackers.url(1))};QVERIFY(source.replaceValue("announce-list",test::encoded(libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(urls)}))));
    auto attrs=parsed(QByteArray::fromStdString(encoded(*source.find("attributes"))));QVERIFY(attrs.replaceValue("pluginoptions","d5:mldhtd14:enableannouncei0eee"));QVERIFY(source.replaceValue("attributes",attrs.serialize()));test::write(fixture.stateFile,QByteArray::fromStdString(source.serialize()));const auto info=encoded(*source.find("info"));const auto payload=test::read(fixture.payload+"/sample.bin");
    auto lease=held(fixture);auto service=owner();engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.udpTrackers=true;p.dht=dhtEnabled;p.bindAddresses={"127.0.0.1"};p.mainlineIpv4=true;p.mainlineIpv6=false;p.mainlinePeerBootstrapOnly=true;
    QTcpServer reserved;QVERIFY(reserved.listen(QHostAddress::LocalHost,0));p.listenPort=reserved.serverPort();reserved.close();trackers.peerPort=p.listenPort;QUdpSocket discovery;QVERIFY(discovery.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));p.mainlinePort=discovery.localPort();discovery.close();
    QVERIFY(service->configureNetwork(p));QVERIFY(service->configureMainlineBootstrap({}));QVERIFY(service->startIdle(lease));QVERIFY(service->startManaged(lease,{.hash=*fixture.hash,.queued=false}));ActiveUdpCleanup cleanup{*service,trackers};
    const auto pump=[&]{const auto result=service->poll();const auto rows=service->managedDownloads();return result&&rows.size()==1&&!rows[0].failure&&trackers.failure.isEmpty();};
    const auto pendingDiagnostic=[&]{const auto rows=service->managedDownloads();const auto detail=rows.empty()?QString("no managed row"):QString("admission=%1 phase=%2 checked=%3 retained=%4 failure=%5").arg(rows[0].admission).arg(static_cast<int>(rows[0].phase)).arg(rows[0].checked).arg(rows[0].retained).arg(rows[0].failure?QString::fromStdString(rows[0].failure->message):QString("none"));return QString("%1 connects=%2 announces=%3 held=%4 scrapes=%5 tracker=%6").arg(detail).arg(trackers.connects).arg(trackers.announces).arg(trackers.held()).arg(trackers.scrapes).arg(trackers.failure);};
    QTRY_VERIFY2_WITH_TIMEOUT(pump()&&trackers.held()&&service->managedDownloads()[0].checked,qPrintable(pendingDiagnostic()),15000);QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));const auto admission=service->managedDownloads()[0].admission;QVERIFY(admission);QCOMPARE(trackers.announces,1);QCOMPARE(trackers.scrapes,0);QVERIFY(!service->managedActiveUdpTrackerResponse(*fixture.hash,admission));
    trackers.failFirst();std::optional<engine::ManagedTrackerUrlResponse> current;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return false;}current=service->managedActiveUdpTrackerResponse(*fixture.hash,admission);return trackers.second>=0&&current&&current->response.content;})(),25000);
    QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));QVERIFY(current&&current->response.content);QCOMPARE(current->url,trackers.url(trackers.second));QVERIFY(trackers.second!=trackers.first);QCOMPARE(current->response.content->seeds,7);QCOMPARE(current->response.content->peers,8);QVERIFY(!service->managedTrackerResponse(*fixture.hash,admission));QVERIFY(!service->managedActiveUdpTrackerResponse(*fixture.hash,admission+1));
    const auto views=service->libraryTrackerResponses();QCOMPARE(views.size(),std::size_t{1});QCOMPARE(views[0].admission,admission);QVERIFY(views[0].response.content);QCOMPARE(views[0].response.content->seeds,7);QCOMPARE(trackers.scrapes,0);QCOMPARE(trackers.announces,2);QVERIFY(service->mainlineScrapeEstimates().empty());const auto mainline=service->mainlineDiscovery();QVERIFY(mainline.admittedScrapeCandidates.empty());QCOMPARE(mainline.lookups,std::uint64_t{0});QCOMPARE(mainline.announces,std::uint64_t{0});QCOMPARE(mainline.deliveredPeers,std::uint64_t{0});
    auto changed=source;QVERIFY(changed.replaceValue("announce-list",test::encoded(libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(trackers.url(0))}),libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(trackers.url(1))})}))));test::write(fixture.stateFile,QByteArray::fromStdString(changed.serialize()));const auto externalState=test::read(fixture.stateFile);QVERIFY(service->rearmMainlineScrape(*fixture.hash));QVERIFY(pump());QVERIFY(!service->managedActiveUdpTrackerResponse(*fixture.hash,admission));QVERIFY(service->libraryTrackerResponses().empty());QCOMPARE(trackers.scrapes,0);
    QVERIFY(service->pauseManaged(*fixture.hash,true));QVERIFY(!service->managedActiveUdpTrackerResponse(*fixture.hash,admission));const auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QVERIFY(closed.checkpointFailure);QCOMPARE(closed.checkpointFailure->code,storage::ErrorCode::Conflict);QCOMPARE(closed.checkpointFailure->message,std::string("Saved state changed since loading"));QCOMPARE(test::read(fixture.stateFile),externalState);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);const auto after=parsed(test::read(fixture.stateFile));QCOMPARE(encoded(*after.find("info")),info);QVERIFY(fixture.accountingIntact(false));QVERIFY2(trackers.failure.isEmpty(),qPrintable(trackers.failure));
  }
  void trackerResponseLifetimeRetainsAcrossAdmissionsAndLossCannotResetToInitialError(){
    test::OwnedEngineFixture fixture(false);auto lease=held(fixture);
    auto files=engine::detail::ManagedStorage::prepare(lease,*fixture.hash,engine::NetworkPolicy{});QVERIFY(files);
    const auto params=files->parameters();QVERIFY(params.ti);const auto section=params.ti->info_section();
    const auto url=std::string("udp://127.0.0.1:12345/announce");const auto key=engine::detail::originalTrackerResponseKey(lease,files->identity(),{section.data(),static_cast<std::size_t>(section.size())},url);QVERIFY(key);
    const auto other=engine::detail::originalTrackerResponseKey(lease,files->identity(),{section.data(),static_cast<std::size_t>(section.size())},"udp://127.0.0.1:12346/announce");QVERIFY(other&&*other!=*key);
    const auto digest=engine::detail::originalTrackerUrlDigest(url);QVERIFY(digest);auto response=std::make_shared<core::QueueOriginalTrackerResponse>();
    engine::detail::ManagedTrackerResponse first(41,1,*digest,response);first.installed=true;
    engine::detail::ManagedTrackerResponse::Value value;value.admission=41;value.sequence=1;value.request=1;value.protocol=1;value.tracker=*digest;value.requestStartedNanos=1;value.observedNanos=1000000000;value.terminal.kind=core::QueueTrackerTerminalKind::ScrapeFailed;value.terminal.definitiveFailure=true;value.terminal.observedMillis=1000;
    std::thread producer([&]{first.observe(value);});producer.join();QVERIFY(first.drain());QVERIFY(response->eligible(1000));QVERIFY(response->publishDht(1,2,42));
    // Actual retirement swaps only the admission ring; library response remains.
    engine::detail::ManagedTrackerResponse second(42,1,*digest,response);second.installed=true;QVERIFY(second.drain());QCOMPARE(response->response().content->seeds,1);QVERIFY(response->eligible(1001));
    value.admission=42;value.sequence=1;value.request=2;value.observedNanos=2000000000;value.terminal.observedMillis=2000;second.observe(value);QVERIFY(second.drain());QCOMPARE(response->response().content->seeds,1);
    value.sequence=3;second.observe(value);QVERIFY(!second.drain());QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Unknown);QVERIFY(!response->eligible(2000));
    engine::detail::ManagedTrackerResponse afterLoss(43,1,*digest,response);afterLoss.installed=true;value.admission=43;value.sequence=1;afterLoss.observe(value);QVERIFY(afterLoss.drain());QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Unknown); // Fresh request failure cannot erase uncertainty.
    value.sequence=2;value.request=3;value.terminal.kind=core::QueueTrackerTerminalKind::Announce;value.terminal.comparable=true;value.terminal.present=3;value.terminal.complete=5;value.terminal.incomplete=6;afterLoss.observe(value);QVERIFY(afterLoss.drain());QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Success);QVERIFY(!response->eligible(2000));
    QVERIFY(fixture.intact(false));
  }
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
