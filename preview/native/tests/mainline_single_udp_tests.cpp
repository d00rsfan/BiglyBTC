#include "library_commands.hpp"
#include "library_udp_policy.hpp"
#include "network_policy.hpp"
#include "owned_mainline_krpc.hpp"
#include "persistent_session.hpp"

#include <QElapsedTimer>
#include <QtTest>

#include <array>
#include <iostream>
#include <set>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;
using test::mainline::OwnedKrpc;

// Authentication reads metadata by design. This snapshot detects writes or
// replacement without mistaking an ordinary atime change for data mutation.
struct SavedFile {
  QString path;
  QByteArray bytes;
  struct stat stamp{};
  explicit SavedFile(QString value) : path(std::move(value)), bytes(test::read(path)) {
    test::require(::lstat(path.toStdString().c_str(), &stamp) == 0 && S_ISREG(stamp.st_mode),
                  "Cannot snapshot generated queued owner file");
  }
  bool unchanged() const {
    struct stat current{};
    return ::lstat(path.toStdString().c_str(), &current) == 0
        && current.st_dev == stamp.st_dev && current.st_ino == stamp.st_ino
        && current.st_mode == stamp.st_mode && current.st_nlink == stamp.st_nlink
        && current.st_size == stamp.st_size
        && current.st_mtim.tv_sec == stamp.st_mtim.tv_sec && current.st_mtim.tv_nsec == stamp.st_mtim.tv_nsec
        && current.st_ctim.tv_sec == stamp.st_ctim.tv_sec && current.st_ctim.tv_nsec == stamp.st_ctim.tv_nsec
        && test::read(path) == bytes;
  }
};

class UnopenedPayload final {
  int descriptor_ = -1;
public:
  explicit UnopenedPayload(const test::OwnedEngineFixture& fixture) {
    descriptor_ = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    test::require(descriptor_ >= 0, "Cannot observe owned payload access");
    constexpr auto mask = IN_OPEN | IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE
        | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF;
    try {
      test::require(::inotify_add_watch(descriptor_, fixture.payload.toStdString().c_str(), mask) >= 0,
                    "Cannot watch generated payload directory");
      for (const auto& file : fixture.files) {
        test::require(::inotify_add_watch(descriptor_, (fixture.payload + '/' + file.relative).toStdString().c_str(), mask) >= 0,
                      "Cannot watch generated real payload file");
      }
    } catch (...) { ::close(descriptor_); descriptor_ = -1; throw; }
  }
  ~UnopenedPayload() { if (descriptor_ >= 0) { ::close(descriptor_); } }
  UnopenedPayload(const UnopenedPayload&) = delete;
  UnopenedPayload& operator=(const UnopenedPayload&) = delete;
  void stop() { if (descriptor_ >= 0) { ::close(descriptor_); descriptor_ = -1; } }
  QString failure() const {
    alignas(inotify_event) std::array<char, 4096> events{};
    const auto count = ::read(descriptor_, events.data(), events.size());
    const auto readError = errno;
    if (count < 0 && readError == EAGAIN) { return {}; }
    if (count < 0) { return QStringLiteral("Payload inotify read failed: %1").arg(QString::fromLocal8Bit(std::strerror(readError))); }
    if (count == 0) { return QStringLiteral("Payload inotify unexpectedly closed"); }
    const auto* first = reinterpret_cast<const inotify_event*>(events.data());
    return QStringLiteral("Queued-only owner touched payload: mask=0x%1, watch=%2, bytes=%3")
        .arg(first->mask, 0, 16).arg(first->wd).arg(count);
  }
};

QByteArray originalCache(unsigned short port) {
  using namespace test::mainline;
  const auto node = dictionary({{"addr", string(compact(port))}, {"id", string(QByteArray(20, 'K'))},
      {"created", "i800e"}, {"lastSeen", "i995e"}, {"failedCount", "i0e"}, {"verified", "i1e"}});
  return dictionary({{"mainEntries", list({node})}, {"replacements", list({})},
      {"oldKey", string(QByteArray(20, 'R'))}, {"timestamp", "i1000e"},
      {"opaque", string(QByteArray::fromHex("00ff51"))}});
}

engine::NetworkPolicy localPolicy() {
  engine::NetworkPolicy result;
  result.mode = engine::NetworkMode::Normal; result.dht = true; result.udpTrackers=true; result.ipv6 = false;
  result.bindAddresses = {"127.0.0.1"}; result.mainlineIpv4 = true; result.mainlineIpv6 = false;
  result.mainlinePeerBootstrapOnly = true;
  QTcpServer transfer, discovery;
  test::require(transfer.listen(QHostAddress::LocalHost, 0) && discovery.listen(QHostAddress::LocalHost, 0),
                "Cannot reserve distinct owned transfer/discovery ports");
  QUdpSocket reserved;
  test::require(reserved.bind(QHostAddress::LocalHost, discovery.serverPort(), QUdpSocket::DontShareAddress),
                "Cannot reserve actual owned Mainline UDP port");
  result.listenPort = transfer.serverPort(); result.mainlinePort = discovery.serverPort();
  return result;
}

std::int64_t millis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int64_t wallMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
// Independent BEP15 responder. It never calls the production wire helpers.
class RegularUdp final {
  QUdpSocket socket_,foreign_;
  QByteArray hash_;
  struct Held {QHostAddress host;unsigned short port;QByteArray packet;};
  std::vector<Held> held_;
  static constexpr std::uint64_t Connection=0x12345678abcdef00ULL;
  static std::uint32_t word(const QByteArray& value,qsizetype pos){std::uint32_t result=0;for(qsizetype i=0;i<4;++i){result=(result<<8)|static_cast<unsigned char>(value[pos+i]);}return result;}
  static void put(QByteArray& value,std::uint32_t x){for(int i=24;i>=0;i-=8){value+=static_cast<char>(x>>i);}}
  void fail(const char* text){if(failure.isEmpty()){failure=QString::fromLatin1(text);}}
  void receive(){
    while(socket_.hasPendingDatagrams()){
      const auto request=socket_.receiveDatagram(2048);const auto p=request.data();
      if(request.senderAddress()!=socket_.localAddress()||request.senderPort()<=0||p.size()<16){fail("Invalid owned regular UDP source/size");continue;}
      const auto action=word(p,8),transaction=word(p,12);QByteArray reply;
      if(action==0){
        if(p.size()!=16||word(p,0)!=0x417||word(p,4)!=0x27101980){fail("Wrong owned BEP15 connect");continue;}++connects;
        put(reply,0);put(reply,transaction);put(reply,static_cast<std::uint32_t>(Connection>>32));put(reply,static_cast<std::uint32_t>(Connection));
      }else if(action==2){
        if(p.size()!=36||word(p,0)!=(Connection>>32)||word(p,4)!=static_cast<std::uint32_t>(Connection)||p.mid(16,20)!=hash_){fail("Wrong owned BEP15 connection/hash");continue;}++scrapes;
        if(error){put(reply,3);put(reply,transaction);reply+="owned regular failure";}
        else{put(reply,2);put(reply,transaction);put(reply,5);put(reply,0);put(reply,6);if(extraTail){reply+='x';}}
      }else{fail("Standalone regular scrape attempted a different action/announce");continue;}
      sourcePorts.insert(static_cast<unsigned short>(request.senderPort()));transactions.insert(transaction);
      if((action==0&&holdConnect)||(action==2&&holdScrape)){held_.push_back({request.senderAddress(),static_cast<unsigned short>(request.senderPort()),reply});continue;}
      if(noise&&action==2){auto wrong=reply;wrong[4]^=1;send(socket_,request.senderAddress(),static_cast<unsigned short>(request.senderPort()),wrong);send(foreign_,request.senderAddress(),static_cast<unsigned short>(request.senderPort()),reply);}
      send(socket_,request.senderAddress(),static_cast<unsigned short>(request.senderPort()),reply);
    }
  }
  void send(QUdpSocket& socket,const QHostAddress& host,unsigned short port,const QByteArray& packet){if(socket.writeDatagram(packet,host,port)!=packet.size()){fail("Owned regular UDP reply failed");}}
public:
  QString failure;int connects=0,scrapes=0;bool error=false,extraTail=false,noise=false,holdConnect=false,holdScrape=false;
  std::set<std::uint32_t> transactions;std::set<unsigned short> sourcePorts;
  explicit RegularUdp(const core::TorrentHash& key,QHostAddress address=QHostAddress::LocalHost):hash_(reinterpret_cast<const char*>(key.bytes().data()),20){
    test::require(socket_.bind(address,0,QUdpSocket::DontShareAddress)&&foreign_.bind(address,0,QUdpSocket::DontShareAddress),"Cannot bind owned standalone tracker");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{receive();});
  }
  std::string url()const{return std::string("udp://")+(socket_.localAddress().protocol()==QAbstractSocket::IPv6Protocol?"[::1]":"127.0.0.1")+":"+std::to_string(socket_.localPort())+"/announce";}
  void release(){holdConnect=false;holdScrape=false;for(const auto& held:held_){send(socket_,held.host,held.port,held.packet);}held_.clear();}
};
} // namespace

class MainlineSingleUdpTests final:public QObject {
  Q_OBJECT
private slots:
  void canonicalWirePreservesOriginalSignedCountsAndDeclinesAmbiguousShapes(){
    namespace wire=engine::detail::udp_tracker_wire;
    const auto connect=wire::connect(7);QCOMPARE(QByteArray(connect.data(),connect.size()),QByteArray::fromHex("00000417271019800000000000000007"));
    const auto connected=wire::reply(QByteArray::fromHex("0000000000000007ffffffffffffffff").toStdString(),7,true);QVERIFY(connected);QCOMPARE(connected->connection,UINT64_MAX);
    const auto result=wire::reply(QByteArray::fromHex("000000020000000780000000ffffffff7fffffff").toStdString(),7,false);QVERIFY(result);QCOMPARE(result->seeds,INT32_MIN);QCOMPARE(result->downloaded,-1);QCOMPARE(result->peers,INT32_MAX);
    QVERIFY(!wire::reply(QByteArray::fromHex("0000000200000008000000010000000200000003").toStdString(),7,false));
    const auto partial=wire::reply(QByteArray::fromHex("00000002000000070000000100000002").toStdString(),7,false);QVERIFY(partial);QCOMPARE(partial->kind,wire::ReplyKind::Unsupported);
    const auto oversized=wire::reply((QByteArray::fromHex("0000000200000007")+QByteArray(1040,'x')).toStdString(),7,false);QVERIFY(oversized);QCOMPARE(oversized->kind,wire::ReplyKind::Unsupported);
    const auto error=wire::reply(QByteArray::fromHex("0000000300000007").toStdString(),7,false);QVERIFY(error);QCOMPARE(error->kind,wire::ReplyKind::Error);
    auto key=core::TorrentHash::parse(std::string(20,'Q'));QVERIFY(key);const auto scrape=wire::scrape(1,9,std::span<const std::byte,20>{key->bytes().data(),20});
    QCOMPARE(QByteArray(scrape.data(),scrape.size()),QByteArray::fromHex("00000000000000010000000200000009")+QByteArray(20,'Q'));
  }
  void ownedPendingRequestRetainsPriorResponseButCannotGrantFromConstructorOrLoss(){
    core::QueueOriginalTrackerResponse response;response.requested();QVERIFY(!response.eligibleDuringOwnedRequest());
    core::QueueTrackerTerminal failed;failed.kind=core::QueueTrackerTerminalKind::ScrapeFailed;failed.definitiveFailure=true;failed.observedMillis=1000;response.observe(failed);QVERIFY(response.eligible(1000));QVERIFY(response.publishDht(2,3,100));
    QVERIFY(!response.eligible(601000));response.requested();QVERIFY(response.eligibleDuringOwnedRequest());QCOMPARE(response.response().content->startedWallMillis,std::int64_t{100});
    response.cancelRequest();QVERIFY(!response.eligibleDuringOwnedRequest());QCOMPARE(response.response().content->seeds,2);response.requested();response.unavailable();QVERIFY(!response.eligibleDuringOwnedRequest());
  }
  void routeAndSavedSettingsNeverGuessDnsProxyVersionOrTrackerOverrides(){
    auto p=localPolicy();const auto url="udp://127.0.0.1:12345/announce";QVERIFY(engine::detail::libraryUdpRoute(p,url));
    for(const auto* bad:{"udp://localhost:12345/announce","udp://127.0.0.1:0/announce","udp://10.0.0.1:12345/announce","udp://224.0.0.1:12345/announce","udp://127.0.0.1:12345/announce?auth=x","http://127.0.0.1:12345/announce"}){const auto result=engine::detail::libraryUdpRoute(p,bad);QVERIFY(!result);QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);}
    p.proxy.kind=engine::NetworkProxyKind::Socks5;QVERIFY(!engine::detail::libraryUdpRoute(p,url));p.proxy.kind=engine::NetworkProxyKind::None;p.udpTrackers=false;QVERIFY(!engine::detail::libraryUdpRoute(p,url));
    QTemporaryDir tmp;QVERIFY(tmp.isValid());auto lease=storage::ProfileLease::acquire(tmp.path().toStdString());QVERIFY(lease);QVERIFY(engine::detail::libraryUdpConfiguration(*lease));
    for(const auto* config:{"d24:Tracker Port UDP Versioni1ee","d28:Tracker Client Scrape Enablei0ee","d35:Tracker Client Scrape Total Disablei1ee","d17:Server Enable UDPi0ee"}){test::write(tmp.path()+"/biglybt.config",config);QVERIFY(!engine::detail::libraryUdpConfiguration(*lease));}
    // Build keys with the independent fixture encoder to avoid length typos.
    test::write(tmp.path()+"/biglybt.config",test::mainline::dictionary({{"Tracker Port UDP Version","i4294967298e"}}));QVERIFY(engine::detail::libraryUdpConfiguration(*lease));
    test::write(tmp.path()+"/alltrackers.config",test::mainline::dictionary({{"trackers",test::mainline::list({test::mainline::dictionary({{"op",test::mainline::dictionary({{"sl","i2e"}})}})})}}));QVERIFY(!engine::detail::libraryUdpConfiguration(*lease));
    test::write(tmp.path()+"/alltrackers.config","de");QVERIFY(engine::detail::libraryUdpConfiguration(*lease));
    test::write(tmp.path()+"/biglybt.config.saving","de");QVERIFY(!engine::detail::libraryUdpConfiguration(*lease));
  }
  void actualWireCancellationFreshAuthorizationMalformedAndDeadlineAreIndependent(){
    auto hash=core::TorrentHash::parse(std::string(20,'Q'));QVERIFY(hash);RegularUdp wire(*hash);wire.noise=true;
    auto route=engine::detail::libraryUdpRoute(localPolicy(),wire.url());QVERIFY(route);engine::detail::LibraryUdpTracker client;
    const auto request=[&](std::uint64_t nonce){return engine::detail::LibraryUdpTracker::Request{*hash,"response",std::string(64,'s'),"configuration",nonce};};
    std::optional<engine::detail::LibraryUdpTracker::Result> actual;bool allowed=true;unsigned authorisations=0;
    const auto pump=[&]{auto result=client.poll(millis(),wallMillis(),[&](const auto&){++authorisations;return allowed;});if(!result){return false;}if(*result){actual=std::move(**result);}return wire.failure.isEmpty();};
    QVERIFY(client.start(request(1),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY2(wire.failure.isEmpty(),qPrintable(wire.failure));QVERIFY(actual);QCOMPARE(actual->request.nonce,std::uint64_t{1});QVERIFY(actual->terminal.comparable);QCOMPARE(actual->terminal.complete,std::int64_t{5});QCOMPARE(actual->terminal.incomplete,std::int64_t{6});QVERIFY(authorisations>=2);QCOMPARE(wire.connects,1);QCOMPARE(wire.scrapes,1);QVERIFY(!client.active());
    core::QueueOriginalTrackerResponse model;model.requested();model.observe(actual->terminal);QCOMPARE(model.response().type,core::QueueOriginalScrapeType::Success);QVERIFY(!model.eligible(millis()));
    wire.noise=false;actual.reset();wire.holdScrape=true;QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||wire.scrapes==2,10000);QVERIFY(!actual);client.cancel();QVERIFY(!client.active());wire.holdScrape=false;
    QVERIFY(client.start(request(3),*route,millis(),wallMillis()));wire.release();QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(actual);QCOMPARE(actual->request.nonce,std::uint64_t{3});QCOMPARE(wire.scrapes,3);QVERIFY(wire.transactions.size()>=2);
    actual.reset();wire.holdConnect=true;QVERIFY(client.start(request(4),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(wire.connects==4,10000);allowed=false;wire.release();QTRY_VERIFY_WITH_TIMEOUT(!pump()||!client.active(),10000);QVERIFY(!actual);QCOMPARE(wire.scrapes,3);allowed=true;
    wire.extraTail=true;QVERIFY(client.start(request(5),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(actual);QVERIFY(!actual->terminal.definitiveFailure&&!actual->terminal.comparable);core::QueueOriginalTrackerResponse ambiguous;ambiguous.requested();ambiguous.observe(actual->terminal);QCOMPARE(ambiguous.response().type,core::QueueOriginalScrapeType::Unknown);QVERIFY(!ambiguous.eligible(millis()));
    actual.reset();wire.extraTail=false;wire.holdConnect=true;const auto start=millis();QVERIFY(client.start(request(6),*route,start,wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(wire.connects==6,10000);
    const auto expired=client.poll(start+engine::detail::LibraryUdpTracker::StageTimeoutMillis,wallMillis(),[](const auto&){return true;});QVERIFY(expired&&*expired);QVERIFY((**expired).terminal.definitiveFailure);QVERIFY(!client.active()); // Unit clock boundary; not an actual30s-runtime claim.
    QVERIFY2(wire.failure.isEmpty(),qPrintable(wire.failure));
  }
  void actualNumericIpv6RoundUsesOnlyTheExplicitIpv6Socket(){
    auto hash=core::TorrentHash::parse(std::string(20,'V'));QVERIFY(hash);RegularUdp wire(*hash,QHostAddress::LocalHostIPv6);auto policy=localPolicy();policy.ipv6=true;policy.bindAddresses={"::1"};
    auto route=engine::detail::libraryUdpRoute(policy,wire.url());QVERIFY(route);QVERIFY(route->local.address().is_v6());QVERIFY(route->remote.address().is_v6());engine::detail::LibraryUdpTracker client;
    engine::detail::LibraryUdpTracker::Request request{*hash,"ipv6",std::string(64,'v'),"configuration",1};QVERIFY(client.start(std::move(request),*route,millis(),wallMillis()));std::optional<engine::detail::LibraryUdpTracker::Result> actual;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return true;}if(*result){actual=std::move(**result);}return actual.has_value()||!wire.failure.isEmpty();})(),10000);
    QVERIFY2(wire.failure.isEmpty(),qPrintable(wire.failure));QVERIFY(actual);QVERIFY(actual->terminal.comparable);QCOMPARE(actual->terminal.complete,std::int64_t{5});QCOMPARE(wire.connects,1);QCOMPARE(wire.scrapes,1);QVERIFY(!client.active());
  }
  void neverAdmittedSavedQueuedUsesActualRegularUdpAndLateReplyCannotGrant(){
    test::OwnedEngineFixture saved(false);RegularUdp tracker(*saved.hash);tracker.error=true;tracker.holdScrape=true;
    auto metadata=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(metadata);QVERIFY(metadata->replaceValue("announce",test::encoded(lt::entry(tracker.url()))));test::write(saved.stateFile,QByteArray::fromStdString(metadata->serialize()));
    const SavedFile initialInventory(saved.profile+"/downloads.config"),state(saved.stateFile),statistics(saved.profile+"/BiglyBT.statistics"),tables(saved.profile+"/tables.config");const auto payload=test::read(saved.payload+"/sample.bin");
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc wire(peer.serverPort());const auto wanted=lt::sha1_hash(reinterpret_cast<const char*>(saved.hash->bytes().data()));wire.allowWithoutPeers(wanted);
    const auto cachePath=saved.profile+"/plugins/mldht/IPv4-table.cache";test::write(cachePath,originalCache(wire.port()));const SavedFile cache(cachePath);
    auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);UnopenedPayload unopened(saved);QElapsedTimer elapsed;elapsed.start();auto created=engine::PersistentSession::create({7951,7952,7953});QVERIFY(created);auto owner=std::move(*created);
    QVERIFY(owner->configureNetwork(localPolicy()));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QString failure;
    const auto pump=[&]{
      if(!owner->poll()){failure="Saved UDP owner poll failed";return false;}const auto status=owner->mainlineDiscovery();
      if(status.failure||status.queuedScrapeFailure){failure=QString::fromStdString(status.failure?status.failure->message:status.queuedScrapeFailure->message);return false;}
      if(!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()){failure="Standalone tracker acquired a hidden admission";return false;}
      failure=unopened.failure()+tracker.failure+wire.failure;return failure.isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||tracker.scrapes==1,25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(owner->mainlineScrapeEstimates().empty());QCOMPARE(wire.scrapes(),0);
    owner->revokeMainlineScrape(*saved.hash);QVERIFY(storage::applyLibraryCommand(*lease,*saved.hash,core::DownloadAction::Stop));QVERIFY(owner->stopManaged(*saved.hash));QVERIFY(owner->rearmMainlineScrape(*saved.hash));
    QVERIFY(storage::applyLibraryCommand(*lease,*saved.hash,core::DownloadAction::Queue));QVERIFY(owner->rearmMainlineScrape(*saved.hash));QCOMPARE(test::read(initialInventory.path),initialInventory.bytes);const SavedFile queuedInventory(initialInventory.path);tracker.release();
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||!owner->mainlineScrapeEstimates().empty(),150000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(tracker.scrapes>=2);QVERIFY(elapsed.elapsed()>=99999);
    const auto estimates=owner->mainlineScrapeEstimates();QCOMPARE(estimates.size(),std::size_t{1});QCOMPARE(estimates[0].hash,*saved.hash);QCOMPARE(estimates[0].counts.responses,std::size_t{1});QCOMPARE(estimates[0].counts.seeds,1);QCOMPARE(estimates[0].counts.peers,1);
    QCOMPARE(wire.scrapes(),1);QCOMPARE(wire.announced(wanted),0);const auto status=owner->mainlineDiscovery();QCOMPARE(status.lookups,std::uint64_t{0});QCOMPARE(status.announces,std::uint64_t{0});QCOMPARE(status.deliveredPeers,std::uint64_t{0});QVERIFY(!peer.hasPendingConnections());
    QVERIFY(queuedInventory.unchanged());QVERIFY(state.unchanged());QVERIFY(cache.unchanged());QVERIFY(statistics.unchanged());QVERIFY(tables.unchanged());
    const auto close=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased&&!close.checkpointFailure);QVERIFY(unopened.failure().isEmpty());QVERIFY(saved.intact(false));unopened.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);
  }
};
int main(int argc,char** argv){
  if(argc==2&&std::string_view(argv[1])=="--udp-wire-projection"){
    namespace wire=engine::detail::udp_tracker_wire;std::string line;std::set<std::string> ids;std::size_t count=0;
    while(std::getline(std::cin,line)){
      if(line.empty()||line.front()=='#'){continue;}const auto a=line.find('\t'),b=line.find('\t',a+1),c=line.find('\t',b+1);
      if(a!=1||line[0]!='U'||b==line.npos||c==line.npos||c!=b+2||(line[b+1]!='C'&&line[b+1]!='S'&&line[b+1]!='E')||line.size()-c-1>2048||!ids.insert(line.substr(a+1,b-a-1)).second||++count>64){return 2;}
      const auto id=line.substr(a+1,b-a-1),text=line.substr(c+1);if(text.size()%2||!std::ranges::all_of(text,[](unsigned char x){return std::isxdigit(x)!=0;})){return 3;}
      const auto packet=QByteArray::fromHex(QByteArray::fromStdString(text));if(packet.size()<8){return 4;}
      const auto raw=packet.toStdString();const auto action=wire::word(raw,0),transaction=wire::word(raw,4);const auto parsed=wire::reply(raw,transaction,line[b+1]=='C');if(!parsed||parsed->kind==wire::ReplyKind::Unsupported){return 5;}
      const auto out=[&](const char* key,std::int64_t value){std::cout<<"OBS\t"<<id<<'\t'<<key<<'\t'<<value<<'\n';};
      out("action",action);out("transaction",std::bit_cast<std::int32_t>(transaction));out("connection",parsed->kind==wire::ReplyKind::Connect?std::bit_cast<std::int64_t>(parsed->connection):-1);
      out("complete",parsed->kind==wire::ReplyKind::Scrape?parsed->seeds:-1);out("downloaded",parsed->kind==wire::ReplyKind::Scrape?parsed->downloaded:-1);out("incomplete",parsed->kind==wire::ReplyKind::Scrape?parsed->peers:-1);
    }
    if(!count){return 6;}std::cout<<"PASS_UDP_TRACKER_NATIVE\t"<<count<<'\n';return 0;
  }
  QCoreApplication app(argc,argv);MainlineSingleUdpTests test;return QTest::qExec(&test,argc,argv);
}
#include "mainline_single_udp_tests.moc"
