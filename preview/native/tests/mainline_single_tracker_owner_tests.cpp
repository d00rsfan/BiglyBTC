#include "network_policy.hpp"
#include "library_udp_tracker.hpp"
#include "library_udp_policy.hpp"
#include "library_http_tracker.hpp"
#include "queued_mainline_scrape.hpp"
#include "owned_mainline_krpc.hpp"
#include "persistent_session.hpp"
#include "persistent_runtime_guard.hpp"
#include "managed_storage.hpp"
#include "managed_tracker_responses.hpp"
#include <cstdint>
#include <set>
#include <QElapsedTimer>
#include <QtTest>
#include <chrono>
#include <QCryptographicHash>
#include <map>
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt=libtorrent;
using test::mainline::OwnedKrpc;

void prepareProfile(test::OwnedEngineFixture& fixture,bool complete,const std::string& tracker){
  auto source=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(source.has_value(),"Cannot parse owned active metadata");
  const auto raw=core::encodeValue(*source->find("attributes"));test::require(raw.has_value(),"Cannot encode owned attributes");
  auto attrs=core::Document::parse(*raw);test::require(attrs.has_value(),"Cannot parse owned attributes");
  test::require(attrs->replaceValue("pluginoptions","d5:mldhtd14:enableannouncei0eee").has_value(),"Cannot set owned zero-announce policy");
  test::require(source->replaceValue("attributes",attrs->serialize()).has_value(),"Cannot update owned attributes");
  test::require(source->replaceValue("announce",test::encoded(lt::entry(tracker))).has_value(),"Cannot set owned configured tracker");
  test::write(fixture.stateFile,QByteArray::fromStdString(source->serialize()));
  auto inventory=core::Document::parse(test::read(fixture.profile+"/downloads.config").toStdString());test::require(inventory.has_value(),"Cannot parse owned active inventory");
  const auto rowBytes=core::encodeValue(inventory->find("downloads")->children[0]);test::require(rowBytes.has_value(),"Cannot encode owned active row");
  auto row=core::Document::parse(*rowBytes);test::require(row.has_value(),"Cannot parse owned active row");
  test::require(row->replaceValue("state",complete?"i60e":"i50e").has_value(),"Cannot set owned active saved state");
  test::require(inventory->replaceValue("downloads","l"+row->serialize()+"e").has_value(),"Cannot update owned active inventory");
  test::write(fixture.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
  if(complete){for(const auto& file:fixture.files){test::write(fixture.payload+'/'+file.relative,file.expected);}}
}
QByteArray cache(unsigned short port){
  using namespace test::mainline;
  const auto node=dictionary({{"addr",string(compact(port))},{"id",string(QByteArray(20,'K'))},
      {"created","i800e"},{"lastSeen","i995e"},{"failedCount","i0e"},{"verified","i1e"}});
  return dictionary({{"mainEntries",list({node})},{"replacements",list({})},{"oldKey",string(QByteArray(20,'R'))},{"timestamp","i1000e"}});
}
engine::NetworkPolicy policy(){
  engine::NetworkPolicy result;result.mode=engine::NetworkMode::Normal;result.dht=true;result.ipv6=false;result.udpTrackers=true;
  result.bindAddresses={"127.0.0.1"};result.mainlineIpv4=true;result.mainlineIpv6=false;result.mainlinePeerBootstrapOnly=true;
  QTcpServer tcp,dht;test::require(tcp.listen(QHostAddress::LocalHost,0)&&dht.listen(QHostAddress::LocalHost,0),"Cannot reserve owned active ports");
  QUdpSocket udp;test::require(udp.bind(QHostAddress::LocalHost,dht.serverPort(),QUdpSocket::DontShareAddress),"Cannot reserve owned active UDP port");
  result.listenPort=tcp.serverPort();result.mainlinePort=dht.serverPort();return result;
}
QByteArray rawInfo(const QString& path){
  const auto bytes=test::read(path);const auto source=core::Document::parse(bytes.toStdString());
  test::require(source.has_value()&&source->find("info"),"Owned active raw info missing");
  const auto* info=source->find("info");return bytes.mid(static_cast<qsizetype>(info->begin),static_cast<qsizetype>(info->end-info->begin));
}

// Independent BEP15 endpoint: actual connect/announce/scrape transactions and
// source sockets are parsed here. No product parser or callback creates replies.
class OwnedUdpTracker final {
  QUdpSocket socket_;
  QByteArray hash_;
  bool failed_;
  unsigned short peerPort_=0;
  static constexpr std::uint64_t Connection=0x1133557799aabbccULL;
  static std::uint32_t read32(const QByteArray& packet,qsizetype offset){
    std::uint32_t result=0;for(qsizetype i=0;i<4;++i){result=(result<<8)|static_cast<unsigned char>(packet[offset+i]);}return result;
  }
  static std::uint64_t read64(const QByteArray& packet,qsizetype offset){
    return (static_cast<std::uint64_t>(read32(packet,offset))<<32)|read32(packet,offset+4);
  }
  static void put32(QByteArray& packet,std::uint32_t value){for(int shift=24;shift>=0;shift-=8){packet+=static_cast<char>(value>>shift);}}
  static void put64(QByteArray& packet,std::uint64_t value){put32(packet,static_cast<std::uint32_t>(value>>32));put32(packet,static_cast<std::uint32_t>(value));}
  void fail(const char* message){if(failure.isEmpty()){failure=QString::fromLatin1(message);}}
  void receive(){
    while(socket_.hasPendingDatagrams()){
      const auto datagram=socket_.receiveDatagram(2048);const auto packet=datagram.data();
      if(datagram.senderAddress()!=QHostAddress::LocalHost||datagram.senderPort()<=0||packet.size()<16||packet.size()>1024){fail("Invalid owned UDP tracker source or packet size");continue;}
      sources.insert(static_cast<unsigned short>(datagram.senderPort()));
      const auto action=read32(packet,8),transaction=read32(packet,12);QByteArray reply;
      if(action==0){
        if(packet.size()!=16||read64(packet,0)!=0x41727101980ULL){fail("Invalid BEP15 connect request");continue;}
        ++connects;put32(reply,0);put32(reply,transaction);put64(reply,Connection);
      }else{
        // LT's address-keyed connection cache can reuse this same fixture ID.
        if(read64(packet,0)!=Connection){fail("Unrecognised BEP15 connection ID");continue;}
        if(action==1){
          if(packet.size()<98||packet.mid(16,20)!=hash_){fail("Wrong announce size or info hash");continue;}
          const auto port=(static_cast<unsigned char>(packet[96])<<8)|static_cast<unsigned char>(packet[97]);
          if(!peerPort_||port!=peerPort_){fail("Announce used a different actual peer listen port");continue;}
          ++announces;
          if(!failed_){put32(reply,1);put32(reply,transaction);put32(reply,300);put32(reply,6);put32(reply,5);}
        }else if(action==2){
          if(packet.size()!=36||packet.mid(16,20)!=hash_){fail("Wrong scrape size or info hash");continue;}
          ++scrapes;
          if(!failed_){put32(reply,2);put32(reply,transaction);put32(reply,5);put32(reply,0);put32(reply,6);}
        }else{fail("Unrequested owned tracker action");continue;}
        if(failed_){put32(reply,3);put32(reply,transaction);reply+="owned tracker failure";}
      }
      if(socket_.writeDatagram(reply,datagram.senderAddress(),datagram.senderPort())!=reply.size()){fail("Owned UDP tracker reply failed");}
    }
  }
public:
  QString failure;
  int connects=0,announces=0,scrapes=0;
  std::set<unsigned short> sources;
  OwnedUdpTracker(const core::TorrentHash& hash,bool failed):hash_(reinterpret_cast<const char*>(hash.bytes().data()),20),failed_(failed){
    test::require(socket_.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Cannot bind owned UDP tracker");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{receive();});
  }
  std::string url()const{return "udp://127.0.0.1:"+std::to_string(socket_.localPort())+"/announce";}
  void peerPort(unsigned short value){test::require(value!=0,"Missing actual tracker peer port");peerPort_=value;}
};
std::int64_t millis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int64_t wallMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
// Independent regular UDP slice fixtures. No protocol helper from production
// constructs these replies. The fixed cookie matches the older owned fixture:
// LT caches it process-wide by IP address, including across fixture ports.
// Standalone scrapes must still have their own actual CONNECT source entry.
class RegularUdpEndpoint final {
  QUdpSocket socket;
  std::map<quint16,std::uint64_t> connections;
  static constexpr std::uint64_t Connection=0x1133557799aabbccULL;
  struct Pending {QByteArray bytes;QHostAddress host;quint16 port;};QList<Pending> held;
  static std::uint32_t word(const QByteArray& p,int at){std::uint32_t n=0;for(int i=0;i<4;++i){n=(n<<8)|static_cast<unsigned char>(p[at+i]);}return n;}
  static std::uint64_t wide(const QByteArray& p,int at){return (std::uint64_t(word(p,at))<<32)|word(p,at+4);}
  static void put(QByteArray& p,std::uint32_t n){for(int shift=24;shift>=0;shift-=8){p+=static_cast<char>(n>>shift);}}
  void send(const Pending& p){if(socket.writeDatagram(p.bytes,p.host,p.port)!=p.bytes.size()){failure="Owned regular UDP reply failed";}}
  void receive(){while(socket.hasPendingDatagrams()){
    const auto d=socket.receiveDatagram(1025);const auto p=d.data();
    if(d.senderAddress()!=QHostAddress::LocalHost||!d.senderPort()||p.size()<16||p.size()>1024){failure="Owned regular UDP source/size mismatch";continue;}
    const auto action=word(p,8),transaction=word(p,12);QByteArray reply;
    if(action==0){if(p.size()!=16||wide(p,0)!=0x41727101980ULL){failure="Owned regular UDP connect mismatch";continue;}
      ++connects;const auto id=Connection;connections[d.senderPort()]=id;put(reply,0);put(reply,transaction);put(reply,static_cast<std::uint32_t>(id>>32));put(reply,static_cast<std::uint32_t>(id));
    }else{
      const auto found=connections.find(d.senderPort());if(wide(p,0)!=Connection||(action==2&&(found==connections.end()||found->second!=Connection))||p.mid(16,20)!=expectedHash){failure="Owned regular UDP connection/hash mismatch";continue;}
      if(action==1){if(p.size()<98||!peerPort||((static_cast<unsigned char>(p[96])<<8)|static_cast<unsigned char>(p[97]))!=peerPort){failure="Owned regular UDP announce/peer-port mismatch";continue;}
        ++announces;if(failAnnounce){put(reply,3);put(reply,transaction);reply+="owned announce failure";}else{put(reply,1);put(reply,transaction);put(reply,1800);put(reply,6);put(reply,5);}
      }else if(action==2){if(p.size()!=36){failure="Owned regular UDP scrape shape mismatch";continue;}++scrapes;put(reply,2);put(reply,transaction);put(reply,7);put(reply,9);put(reply,8);}
      else{failure="Owned regular UDP unexpected action";continue;}
    }
    Pending pending{reply,d.senderAddress(),d.senderPort()};if((action==0&&holdConnect)||(action==2&&holdScrape)){held.push_back(std::move(pending));}else{send(pending);}
  }}
public:
  QByteArray expectedHash;QString failure;int connects=0,announces=0,scrapes=0;quint16 peerPort=0;
  bool holdConnect=false,holdScrape=false,failAnnounce=true;
  RegularUdpEndpoint(){test::require(socket.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Cannot bind regular UDP fixture");QObject::connect(&socket,&QUdpSocket::readyRead,&socket,[this]{receive();});}
  std::string url()const{return "udp://127.0.0.1:"+std::to_string(socket.localPort())+"/announce";}
  void release(){holdConnect=false;holdScrape=false;const auto copy=held;held.clear();for(const auto& p:copy){send(p);}}
};
void prepareRegularUdp(test::OwnedEngineFixture& saved,const std::string& url,bool privateTorrent,bool dhtAllowed,bool admitted){
  prepareProfile(saved,false,url);auto source=core::Document::parse(test::read(saved.stateFile).toStdString());test::require(source.has_value(),"Regular UDP metadata parse");
  if(privateTorrent){const auto raw=core::encodeValue(*source->find("info"));test::require(raw.has_value(),"Regular UDP private info encode");auto info=core::Document::parse(*raw);test::require(info.has_value()&&info->replaceValue("private","i1e").has_value(),"Regular UDP private flag");test::require(source->replaceValue("info",info->serialize()).has_value(),"Regular UDP private info replace");
    const auto rawHash=QCryptographicHash::hash(QByteArray::fromStdString(info->serialize()),QCryptographicHash::Sha1);const auto hash=core::TorrentHash::parse(std::string_view(rawHash.constData(),static_cast<std::size_t>(rawHash.size())));test::require(hash.has_value(),"Regular UDP private hash");const auto old=saved.stateFile;saved.hash=*hash;saved.stateFile=saved.profile+"/active/"+QString::fromStdString(hash->hex())+".dat";test::require(QFile::remove(old),"Regular UDP old owned source removal");}
  const auto raw=core::encodeValue(*source->find("attributes"));test::require(raw.has_value(),"Regular UDP attrs encode");auto attrs=core::Document::parse(*raw);test::require(attrs.has_value(),"Regular UDP attrs parse");
  test::require(attrs->replaceValue("pluginoptions","d5:mldhtd14:enableannouncei1eee").has_value()
      &&attrs->replaceValue("peersources",dhtAllowed?"l7:Tracker3:DHTe":"l7:Trackere").has_value()
      &&attrs->replaceValue("peersourcesdenied",dhtAllowed?"le":"l3:DHTe").has_value()
      &&source->replaceValue("attributes",attrs->serialize()).has_value(),"Regular UDP source permissions");test::write(saved.stateFile,QByteArray::fromStdString(source->serialize()));
  auto inventory=core::Document::parse(test::read(saved.profile+"/downloads.config").toStdString());test::require(inventory.has_value(),"Regular UDP inventory parse");const auto bytes=core::encodeValue(inventory->find("downloads")->children[0]);test::require(bytes.has_value(),"Regular UDP row encode");auto row=core::Document::parse(*bytes);test::require(row.has_value(),"Regular UDP row parse");
  const auto rawHash=std::string(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);test::require(row->replaceValue("torrent_hash",test::encoded(lt::entry(rawHash))).has_value()&&row->replaceValue("torrent",test::encoded(lt::entry(saved.stateFile.toStdString()))).has_value()&&row->replaceValue("state",admitted?"i50e":"i75e").has_value()&&inventory->replaceValue("downloads","l"+row->serialize()+"e").has_value(),"Regular UDP live/saved intent");test::write(saved.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
}
struct RegularPayloadWatch {
  int fd=-1;
  explicit RegularPayloadWatch(const test::OwnedEngineFixture& fixture){fd=::inotify_init1(IN_NONBLOCK|IN_CLOEXEC);test::require(fd>=0,"Regular UDP payload watch");constexpr auto mask=IN_OPEN|IN_ACCESS|IN_MODIFY|IN_ATTRIB|IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO;test::require(::inotify_add_watch(fd,fixture.payload.toStdString().c_str(),mask)>=0,"Regular UDP root watch");for(const auto& file:fixture.files){test::require(::inotify_add_watch(fd,(fixture.payload+'/'+file.relative).toStdString().c_str(),mask)>=0,"Regular UDP file watch");}}
  ~RegularPayloadWatch(){stop();}void stop(){if(fd>=0){::close(fd);fd=-1;}}bool untouched()const{char events[4096];return ::read(fd,events,sizeof(events))<0&&errno==EAGAIN;}
};
QByteArray regularUdpSettings(bool dht){using namespace test::mainline;return dictionary({{"Peer Source Selection Default.DHT",dht?"i1e":"i0e"},{"Tracker Client Enable TCP","i0e"}});}
struct RegularOwnerCleanup {engine::PersistentSession& owner;RegularUdpEndpoint& endpoint;~RegularOwnerCleanup(){endpoint.release();static_cast<void>(owner.close(std::chrono::steady_clock::now()+10s));}};
} // namespace

class MainlineSingleTrackerOwnerTests final:public QObject {
  Q_OBJECT
private slots:
  void directRegularUdpSuccessfulScrapeUsesTheOriginalSharedInterval(){
    const auto delay=engine::detail::libraryTrackerScrapeDelay(7);QVERIFY(delay);QVERIFY(*delay>=970000&&*delay<=1149000);QCOMPARE(engine::detail::libraryTrackerScrapeDelay(7),delay);
    auto p=policy();p.dht=false;p.httpTrackers=false;const auto url="udp://127.0.0.1:12345/announce";
    QVERIFY(!engine::detail::libraryUdpRoute(p,url));QVERIFY(engine::detail::libraryUdpRoute(p,url,true));p.proxy.kind=engine::NetworkProxyKind::Socks5;QVERIFY(!engine::detail::libraryUdpRoute(p,url,true));
  }
  void directRegularUdpSavedProxyChangesCannotKeepDirectAuthority(){
    test::OwnedEngineFixture saved(false);auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);auto p=policy();p.dht=false;p.httpTrackers=false;test::write(saved.profile+"/biglybt.config",regularUdpSettings(false));
    auto before=engine::detail::libraryUdpConfiguration(*lease,&p);QVERIFY(before);auto config=core::Document::parse(regularUdpSettings(false).toStdString());QVERIFY(config);QVERIFY(config->replaceValue("Proxy.Password",test::encoded(lt::entry("owned-unused-password"))));test::write(saved.profile+"/biglybt.config",QByteArray::fromStdString(config->serialize()));
    auto changed=engine::detail::libraryUdpConfiguration(*lease,&p);QVERIFY(changed);QVERIFY(*changed!=*before);QVERIFY(changed->find("owned-unused-password")==std::string::npos);
    QVERIFY(config->replaceValue("Enable.Proxy","i1e"));QVERIFY(config->replaceValue("Enable.SOCKS","i1e"));QVERIFY(config->replaceValue("Proxy.Host",test::encoded(lt::entry("127.0.0.1"))));QVERIFY(config->replaceValue("Proxy.Port",test::encoded(lt::entry("12345"))));test::write(saved.profile+"/biglybt.config",QByteArray::fromStdString(config->serialize()));QVERIFY(!engine::detail::libraryUdpConfiguration(*lease,&p));
  }
  void directRegularUdpSavedRowsRequireTrackerButNotDhtOrPayload_data(){
    QTest::addColumn<int>("mode");QTest::newRow("global-DHT-disabled")<<0;QTest::newRow("private")<<1;QTest::newRow("DHT-source-denied")<<2;
  }
  void directRegularUdpSavedRowsRequireTrackerButNotDhtOrPayload(){
    QFETCH(int,mode);test::OwnedEngineFixture saved(false);RegularUdpEndpoint endpoint;prepareRegularUdp(saved,endpoint.url(),mode==1,mode==0,false);endpoint.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);auto p=policy();p.httpTrackers=false;p.dht=mode!=0;test::write(saved.profile+"/biglybt.config",regularUdpSettings(p.dht));
    const auto inventory=test::read(saved.profile+"/downloads.config"),state=test::read(saved.stateFile),payload=test::read(saved.payload+"/sample.bin"),stats=test::read(saved.profile+"/BiglyBT.statistics");RegularPayloadWatch watch(saved);
    auto made=engine::PersistentSession::create({8981,8982,8983});QVERIFY(made);auto owner=std::move(*made);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));{auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);QVERIFY(owner->startIdle(*lease));}RegularOwnerCleanup cleanup{*owner,endpoint};QString failure;
    const auto pump=[&]{if(!owner->poll()){failure="Regular saved UDP poll failed";return false;}if(!watch.untouched()||!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()){failure="Regular saved UDP acquired payload/admission";return false;}failure=endpoint.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||!owner->libraryTrackerResponses().empty(),20000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto views=owner->libraryTrackerResponses();QCOMPARE(views.size(),std::size_t{1});QCOMPARE(views[0].hash,*saved.hash);QCOMPARE(views[0].admission,std::uint64_t{0});QCOMPARE(views[0].response.type,core::QueueOriginalScrapeType::Success);QVERIFY(views[0].response.content);QCOMPARE(views[0].response.content->seeds,7);QCOMPARE(views[0].response.content->peers,8);QCOMPARE(endpoint.connects,1);QCOMPARE(endpoint.scrapes,1);QCOMPARE(endpoint.announces,0);
    const auto discovery=owner->mainlineDiscovery();QCOMPARE(discovery.lookups,std::uint64_t{0});QCOMPARE(discovery.announces,std::uint64_t{0});QCOMPARE(discovery.deliveredPeers,std::uint64_t{0});QVERIFY(owner->mainlineScrapeEstimates().empty());owner->revokeMainlineScrape(*saved.hash);QVERIFY(owner->libraryTrackerResponses().empty());
    QCOMPARE(test::read(saved.profile+"/downloads.config"),inventory);QCOMPARE(test::read(saved.stateFile),state);QCOMPARE(test::read(saved.profile+"/BiglyBT.statistics"),stats);const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(watch.untouched());watch.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);QVERIFY(saved.intact(false));
  }
  void directRegularUdpActualAdmissionSharesResponseAndCannotDeliverAfterPause_data(){
    QTest::addColumn<int>("mode");QTest::newRow("DHT-disabled-real-announce-failure-then-scrape")<<0;QTest::newRow("private-real-announce-failure-then-scrape")<<1;QTest::newRow("private-pause-before-held-scrape")<<2;
  }
  void directRegularUdpActualAdmissionSharesResponseAndCannotDeliverAfterPause(){
    QFETCH(int,mode);QVERIFY(engine::detail::managedTrackerResponseRuntimePin());test::OwnedEngineFixture saved(false);RegularUdpEndpoint endpoint;endpoint.holdScrape=mode==2;prepareRegularUdp(saved,endpoint.url(),mode!=0,mode==0,true);endpoint.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);auto p=policy();p.httpTrackers=false;p.dht=mode!=0;endpoint.peerPort=p.listenPort;test::write(saved.profile+"/biglybt.config",regularUdpSettings(p.dht));const auto info=rawInfo(saved.stateFile),payload=test::read(saved.payload+"/sample.bin");
    auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);auto made=engine::PersistentSession::create({8991,8992,8993});QVERIFY(made);auto owner=std::move(*made);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QVERIFY(owner->startManaged(*lease,{.hash=*saved.hash,.queued=false}));RegularOwnerCleanup cleanup{*owner,endpoint};QString failure;
    const auto pump=[&]{if(!owner->poll()){failure="Regular admitted UDP poll failed";return false;}for(const auto& row:owner->managedDownloads()){if(row.failure){failure=QString::fromStdString(row.failure->message);return false;}}failure=endpoint.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return true;}const auto rows=owner->managedDownloads();return rows.size()==1&&rows[0].checked&&rows[0].retained&&rows[0].phase==engine::ManagedPhase::Downloading;}()),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto admission=owner->managedDownloads()[0].admission;QVERIFY(admission);
    if(mode==2){QTRY_VERIFY_WITH_TIMEOUT(!pump()||endpoint.scrapes==1,15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(owner->pauseManaged(*saved.hash,true));QVERIFY(owner->libraryTrackerResponses().empty());endpoint.release();QVERIFY(pump());const auto late=owner->managedTrackerResponse(*saved.hash,admission);QVERIFY(!late||late->type!=core::QueueOriginalScrapeType::Success);QVERIFY(owner->libraryTrackerResponses().empty());}
    else{QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return true;}const auto views=owner->libraryTrackerResponses();return views.size()==1&&views[0].admission==admission&&views[0].response.content&&views[0].response.content->seeds==7&&views[0].response.content->peers==8;}()),20000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto actual=owner->managedTrackerResponse(*saved.hash,admission);QVERIFY(actual&&actual->content);QCOMPARE(actual->content->seeds,7);QCOMPARE(actual->content->peers,8);QVERIFY(!owner->managedTrackerResponse(*saved.hash,admission+1));QCOMPARE(endpoint.scrapes,1);QVERIFY(endpoint.announces>=1);QVERIFY(owner->pauseManaged(*saved.hash,true));QVERIFY(owner->libraryTrackerResponses().empty());}
    const auto discovery=owner->mainlineDiscovery();QCOMPARE(discovery.lookups,std::uint64_t{0});QCOMPARE(discovery.announces,std::uint64_t{0});QCOMPARE(discovery.deliveredPeers,std::uint64_t{0});QVERIFY(owner->mainlineScrapeEstimates().empty());const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QCOMPARE(rawInfo(saved.stateFile),info);QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);QVERIFY(saved.accountingIntact(false));QVERIFY(endpoint.failure.isEmpty());
  }
  void directRegularUdpFreshTrackerDenialCancelsConnectOrReply_data(){
    QTest::addColumn<bool>("afterConnect");QTest::newRow("deny-before-CONNECT-consumed")<<false;QTest::newRow("deny-before-scrape-terminal")<<true;
  }
  void directRegularUdpFreshTrackerDenialCancelsConnectOrReply(){
    QFETCH(bool,afterConnect);test::OwnedEngineFixture saved(false);RegularUdpEndpoint endpoint;endpoint.holdConnect=!afterConnect;endpoint.holdScrape=afterConnect;prepareRegularUdp(saved,endpoint.url(),true,false,false);endpoint.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);auto p=policy();p.dht=false;p.httpTrackers=false;test::write(saved.profile+"/biglybt.config",regularUdpSettings(false));auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);
    auto targets=engine::readCurrentMainlineScrapeTargets(*lease,{},true,nullptr,engine::SavedTrackerPurpose::RegularUdp);QVERIFY(targets&&targets->size()==1);const auto target=targets->front();auto route=engine::detail::libraryUdpRoute(p,target.configuredTracker,true);QVERIFY(route);QVERIFY(!engine::detail::libraryUdpRoute(p,target.configuredTracker));engine::detail::LibraryUdpTracker client;QVERIFY(client.start({target.hash,target.trackerResponseKey,target.scope,"owned-config",1,true},*route,millis(),wallMillis()));bool withdrew=false;QString failure;
    const auto authorise=[&](const auto&){auto fresh=engine::readCurrentMainlineScrapeTargets(*lease,{},true,nullptr,engine::SavedTrackerPurpose::RegularUdp);const bool current=fresh&&!fresh->empty();withdrew|=!current;return current;};
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),authorise);if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){failure="Denied UDP operation produced a terminal";return false;}failure=endpoint.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||(afterConnect?endpoint.scrapes==1:endpoint.connects==1),10000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    auto source=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(source);const auto raw=core::encodeValue(*source->find("attributes"));QVERIFY(raw);auto attrs=core::Document::parse(*raw);QVERIFY(attrs);QVERIFY(attrs->replaceValue("peersourcesdenied","l3:DHT7:Trackere"));QVERIFY(source->replaceValue("attributes",attrs->serialize()));test::write(saved.stateFile,QByteArray::fromStdString(source->serialize()));endpoint.release();
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||withdrew,10000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(withdrew);QVERIFY(!client.active());QCOMPARE(endpoint.scrapes,afterConnect?1:0);QCOMPARE(endpoint.announces,0);
  }
  void configuredCandidateRequiresSameFullSubjectUrlPolicyAndRealResponseOwner(){
    test::OwnedEngineFixture fixture(false);const std::string url="udp://127.0.0.1:12345/announce";prepareProfile(fixture,false,url);
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto prepared=engine::detail::ManagedStorage::prepare(*lease,*fixture.hash,policy());QVERIFY(prepared);
    engine::ManagedMainlineScrapeObservation admitted{&*prepared,19,false};
    const auto read=[&]{return engine::readCurrentMainlineScrapeTargets(*lease,std::span(&admitted,1),true);};
    const auto state=test::read(fixture.stateFile),inventory=test::read(fixture.profile+"/downloads.config"),payload=test::read(fixture.payload+"/sample.bin");
    auto old=engine::readCurrentMainlineScrapeTargets(*lease,std::span(&admitted,1));QVERIFY(old);QVERIFY(old->empty()); // Old default API remains trackerless.
    auto candidate=read();QVERIFY(candidate);QCOMPARE(candidate->size(),std::size_t{1});QCOMPARE(candidate->front().configuredTracker,url);QVERIFY(!candidate->front().trackerResponseKey.empty());
    const auto params=prepared->parameters();QVERIFY(params.ti);const auto section=params.ti->info_section();
    const auto key=engine::detail::originalTrackerResponseKey(*lease,prepared->identity(),{section.data(),static_cast<std::size_t>(section.size())},url);QVERIFY(key);QCOMPARE(*key,candidate->front().trackerResponseKey);
    core::QueueOriginalTrackerResponse initial;QVERIFY(!initial.eligible(0)); // An authenticated candidate is not response authority.
    auto changed=core::Document::parse(state.toStdString());QVERIFY(changed);QVERIFY(changed->replaceValue("announce-list",test::encoded(lt::entry(lt::entry::list_type{lt::entry(lt::entry::list_type{lt::entry(url),lt::entry("udp://127.0.0.1:12346/announce")})}))));
    test::write(fixture.stateFile,QByteArray::fromStdString(changed->serialize()));auto multi=read();QVERIFY(multi);QVERIFY(multi->empty());test::write(fixture.stateFile,state);
    const auto denied=[&](const char* keyName,const char* encoded){
      auto doc=core::Document::parse(state.toStdString());if(!doc){return false;}const auto attrs=core::encodeValue(*doc->find("attributes"));if(!attrs){return false;}
      auto edited=core::Document::parse(*attrs);if(!edited||!edited->replaceValue(keyName,encoded)||!doc->replaceValue("attributes",edited->serialize())){return false;}
      test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));const auto result=read();return result&&result->empty();
    };
    QVERIFY(denied("peersources","l3:DHTe"));QVERIFY(denied("peersourcesdenied","l7:Trackere"));QVERIFY(denied("pluginoptions","d5:mldhtd14:enableannouncei1eee"));test::write(fixture.stateFile,state);
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);QCOMPARE(test::read(fixture.stateFile),state);QVERIFY(fixture.intact(false));
  }
#if BIGLYBTC_TEST_ORIGINAL_MAINLINE_READER
  void retainedOriginalLeaseKeepsTheActualAdmittedTrackerScrapeCandidate(){
    QVERIFY(engine::detail::managedTrackerResponseRuntimePin());
    test::OwnedEngineFixture fixture(false);OwnedUdpTracker tracker(*fixture.hash,true);
    prepareProfile(fixture,false,tracker.url());
    // A real imported original root, separate from the actual admission's
    // caller object. configureOriginal retains the SAME acquired OFD lock.
    const auto root=fixture.profile+"/plugins/mldht/baseID.config";
    const QByteArray rootBytes=QByteArray(20,'r').toHex().toUpper()+"\n";test::write(root,rootBytes);
    const auto info=rawInfo(fixture.stateFile),payload=test::read(fixture.payload+"/sample.bin");
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto actual=engine::PersistentSession::create({7921,7922,7923});QVERIFY(actual);
    auto selected=policy();tracker.peerPort(selected.listenPort);
    QVERIFY((*actual)->configureNetwork(selected));QVERIFY((*actual)->configureMainlineBootstrap({}));
    QVERIFY((*actual)->configureOriginalMainlineIdentity(*lease));
    QVERIFY((*actual)->startIdle(*lease));QVERIFY((*actual)->startManaged(*lease,{.hash=*fixture.hash,.queued=false}));
    QString failure;
    const auto pump=[&]{
      if(!(*actual)->poll()){failure="Retained original/admitted owner poll failed";return false;}
      const auto status=(*actual)->mainlineDiscovery();
      if(status.failure||status.queuedScrapeFailure){failure=QString::fromStdString(status.failure?status.failure->message:status.queuedScrapeFailure->message);return false;}
      if(!tracker.failure.isEmpty()){failure=tracker.failure;return false;}
      for(const auto& value:(*actual)->managedDownloads()){if(value.failure){failure=QString::fromStdString(value.failure->message);return false;}}
      return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto values=(*actual)->managedDownloads();
      return values.size()==1&&values[0].checked&&values[0].phase==engine::ManagedPhase::Downloading
          &&tracker.announces>0&&tracker.scrapes>0&&(*actual)->mainlineDiscovery().originalIdentityActive;
    }()),25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    const auto admission=(*actual)->managedDownloads()[0].admission;QVERIFY(admission);
    // Existing10s refresh only. No production startup/check clock is replaced
    // and no100s DHT scrape is needed to observe the genuinely eligible row.
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto status=(*actual)->mainlineDiscovery();
      return status.admittedScrapeCandidates.size()==1&&status.admittedScrapeCandidates[0].hash==*fixture.hash
          &&status.admittedScrapeCandidates[0].admission==admission;
    }()),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    const auto observed=(*actual)->mainlineDiscovery();QVERIFY(observed.originalIdentityActive);
    QCOMPARE(observed.originalIdentityAdoptions,std::uint64_t{1});
    QCOMPARE(observed.lookups,std::uint64_t{0});QCOMPARE(observed.announces,std::uint64_t{0});QCOMPARE(observed.deliveredPeers,std::uint64_t{0});
    QVERIFY((*actual)->mainlineScrapeEstimates().empty());
    const auto values=(*actual)->managedDownloads();QCOMPARE(values[0].admission,admission);
    QCOMPARE(values[0].received,std::int64_t{0});QCOMPARE(values[0].sent,std::int64_t{0});
    QVERIFY((*actual)->pauseManaged(*fixture.hash,true));QVERIFY((*actual)->mainlineDiscovery().admittedScrapeCandidates.empty());
    const auto closed=(*actual)->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    const auto retired=(*actual)->mainlineDiscovery();QVERIFY(retired.admittedScrapeCandidates.empty());QVERIFY(retired.originalIdentityJoined);
    QCOMPARE(retired.originalIdentityRetirements,std::uint64_t{1});
    QCOMPARE(test::read(root),rootBytes);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);QCOMPARE(rawInfo(fixture.stateFile),info);
    QVERIFY(fixture.accountingIntact(false));
  }
#endif
  void actualFailedUdpResponseAllowsScrapeWhileHealthyResponseSuppressesIt(){
    // A full catalog/mapping/source-image proof is mandatory. The marker and
    // SDK alone cannot turn this into an accepted configured-tracker test.
    QVERIFY(engine::detail::managedTrackerResponseRuntimePin());
    test::OwnedEngineFixture failed(false),healthy(false);
    OwnedUdpTracker failedTracker(*failed.hash,true),healthyTracker(*healthy.hash,false);
    prepareProfile(failed,false,failedTracker.url());prepareProfile(healthy,false,healthyTracker.url());
    const auto failedInfo=rawInfo(failed.stateFile),healthyInfo=rawInfo(healthy.stateFile);
    const auto failedPayload=test::read(failed.payload+"/sample.bin"),healthyPayload=test::read(healthy.payload+"/sample.bin");
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc failedWire(peer.serverPort()),healthyWire(peer.serverPort());
    const auto failedHash=lt::sha1_hash(reinterpret_cast<const char*>(failed.hash->bytes().data()));
    const auto healthyHash=lt::sha1_hash(reinterpret_cast<const char*>(healthy.hash->bytes().data()));
    failedWire.allowWithoutPeers(failedHash);healthyWire.allowWithoutPeers(healthyHash);
    test::write(failed.profile+"/plugins/mldht/IPv4-table.cache",cache(failedWire.port()));
    test::write(healthy.profile+"/plugins/mldht/IPv4-table.cache",cache(healthyWire.port()));
    auto failedLease=storage::ProfileLease::acquire(failed.native()),healthyLease=storage::ProfileLease::acquire(healthy.native());QVERIFY(failedLease);QVERIFY(healthyLease);
    QElapsedTimer elapsed;elapsed.start();
    auto a=engine::PersistentSession::create({7901,7902,7903}),b=engine::PersistentSession::create({7911,7912,7913});QVERIFY(a);QVERIFY(b);
    auto bad=std::move(*a),good=std::move(*b);const auto badPolicy=policy(),goodPolicy=policy();
    failedTracker.peerPort(badPolicy.listenPort);healthyTracker.peerPort(goodPolicy.listenPort);
    QVERIFY(bad->configureNetwork(badPolicy));QVERIFY(good->configureNetwork(goodPolicy));
    QVERIFY(bad->configureMainlineBootstrap({}));QVERIFY(good->configureMainlineBootstrap({}));
    QVERIFY(bad->startIdle(*failedLease));QVERIFY(good->startIdle(*healthyLease));
    QVERIFY(bad->startManaged(*failedLease,{.hash=*failed.hash,.queued=false}));
    QVERIFY(good->startManaged(*healthyLease,{.hash=*healthy.hash,.queued=false}));
    QString failure;
    const auto pump=[&]{
      for(auto* owner:{bad.get(),good.get()}){
        if(!owner->poll()){failure="Owned single tracker poll failed";return false;}
        const auto state=owner->mainlineDiscovery();
        if(state.failure||state.queuedScrapeFailure){failure=QString::fromStdString(state.failure?state.failure->message:state.queuedScrapeFailure->message);return false;}
        for(const auto& value:owner->managedDownloads()){if(value.failure){failure=QString::fromStdString(value.failure->message);return false;}}
      }
      if(!failedTracker.failure.isEmpty()||!healthyTracker.failure.isEmpty()||!failedWire.failure.isEmpty()||!healthyWire.failure.isEmpty()){
        failure=failedTracker.failure+healthyTracker.failure+failedWire.failure+healthyWire.failure;return false;
      }return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto x=bad->managedDownloads(),y=good->managedDownloads();
      return x.size()==1&&y.size()==1&&x[0].checked&&y[0].checked
          &&x[0].phase==engine::ManagedPhase::Downloading&&y[0].phase==engine::ManagedPhase::Downloading
          &&failedTracker.announces>0&&failedTracker.scrapes>0&&healthyTracker.announces>0;
    }()),25000);
    QVERIFY2(failure.isEmpty(),qPrintable(failure));
    const auto badAdmission=bad->managedDownloads()[0].admission,goodAdmission=good->managedDownloads()[0].admission;
    QVERIFY(badAdmission);QVERIFY(goodAdmission);
    const auto describeResponse=[](const std::optional<core::QueueOriginalScrapeResponse>& value){
      if(!value){return QStringLiteral("absent");}
      QString result=QStringLiteral("type=%1").arg(static_cast<int>(value->type));
      if(value->content){result+=QStringLiteral(" seeds=%1 peers=%2").arg(value->content->seeds).arg(value->content->peers);}
      else{result+=QStringLiteral(" content=absent");}return result;
    };
    const auto healthyDiagnostic=[&]{
      const auto status=good->managedDownloads();std::optional<core::QueueOriginalScrapeResponse> legacy;
      if(status.size()==1&&status[0].queueSourceFacts&&status[0].queueSourceFacts->singleTrackerResponse){legacy=status[0].queueSourceFacts->singleTrackerResponse->response;}
      return QStringLiteral("healthy UDP connect=%1 announce=%2 scrape=%3 phase=%4 retained=[%5] legacy96=[%6] failure=[%7]")
          .arg(healthyTracker.connects).arg(healthyTracker.announces).arg(healthyTracker.scrapes)
          .arg(status.size()==1?static_cast<int>(status[0].phase):-1)
          .arg(describeResponse(good->managedTrackerResponse(*healthy.hash,goodAdmission)))
          .arg(describeResponse(legacy)).arg(failure);
    };
    // The old96 callback deliberately cannot retain exact scrape contents.
    // Assert the SAME rich library response used by the configured-target gate.
    QTRY_VERIFY2_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto response=good->managedTrackerResponse(*healthy.hash,goodAdmission);
      return response&&response->type==core::QueueOriginalScrapeType::Success&&response->content
          &&response->content->seeds==5&&response->content->peers==6;
    }()),qPrintable(healthyDiagnostic()),5000);
    QVERIFY(!good->managedTrackerResponse(*healthy.hash,goodAdmission+1));
    QVERIFY2(failure.isEmpty(),qPrintable(failure));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{return !pump()||!bad->mainlineScrapeEstimates().empty();}()),135000);
    QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY2(elapsed.elapsed()>=99999,"Production startup barrier was bypassed");
    const auto estimate=bad->mainlineScrapeEstimates();QCOMPARE(estimate.size(),std::size_t{1});
    QCOMPARE(estimate[0].hash,*failed.hash);QCOMPARE(estimate[0].counts.responses,std::size_t{1});
    QCOMPARE(estimate[0].counts.seeds,1);QCOMPARE(estimate[0].counts.peers,1);QVERIFY(estimate[0].startedWallMillis>0);
    QCOMPARE(failedWire.scrapes(),1);QCOMPARE(failedWire.gets(failedHash),1);
    // The other real owner has passed the same production startup barrier.
    // Its actual valid announce counts survive any failed RPC; no synthetic
    // HTTP/Java cache or absence-of-response is used as this permission fact.
    QVERIFY(good->mainlineScrapeEstimates().empty());QCOMPARE(healthyWire.scrapes(),0);QCOMPARE(healthyWire.gets(healthyHash),0);
    for(auto* owner:{bad.get(),good.get()}){
      const auto state=owner->mainlineDiscovery();QCOMPARE(state.lookups,std::uint64_t{0});QCOMPARE(state.announces,std::uint64_t{0});QCOMPARE(state.deliveredPeers,std::uint64_t{0});
      const auto values=owner->managedDownloads();QCOMPARE(values.size(),std::size_t{1});QCOMPARE(values[0].received,std::int64_t{0});QCOMPARE(values[0].sent,std::int64_t{0});
    }
    QCOMPARE(bad->managedDownloads()[0].admission,badAdmission);QCOMPARE(good->managedDownloads()[0].admission,goodAdmission);QVERIFY(!peer.hasPendingConnections());
    QVERIFY(bad->pauseManaged(*failed.hash,true));QVERIFY(bad->mainlineScrapeEstimates().empty());QVERIFY(good->stopManaged(*healthy.hash));
    QVERIFY(!good->managedTrackerResponse(*healthy.hash,goodAdmission));
    const auto ca=bad->close(std::chrono::steady_clock::now()+10s),cb=good->close(std::chrono::steady_clock::now()+10s);
    QVERIFY(ca.sessionJoined&&ca.retirement.storageReleased&&!ca.checkpointFailure);QVERIFY(cb.sessionJoined&&cb.retirement.storageReleased&&!cb.checkpointFailure);
    QVERIFY(!bad->managedTrackerResponse(*failed.hash,badAdmission));QVERIFY(!good->managedTrackerResponse(*healthy.hash,goodAdmission));
    QCOMPARE(test::read(failed.payload+"/sample.bin"),failedPayload);QCOMPARE(test::read(healthy.payload+"/sample.bin"),healthyPayload);
    QCOMPARE(rawInfo(failed.stateFile),failedInfo);QCOMPARE(rawInfo(healthy.stateFile),healthyInfo);QVERIFY(failed.accountingIntact(false));QVERIFY(healthy.accountingIntact(false));
    qInfo("actual configured UDP: failed announces=%d scrapes=%d, healthy announces=%d scrapes=%d, DHT=%d/%d",
        failedTracker.announces,failedTracker.scrapes,healthyTracker.announces,healthyTracker.scrapes,failedWire.scrapes(),healthyWire.scrapes());
  }
};
QTEST_GUILESS_MAIN(MainlineSingleTrackerOwnerTests)
#include "mainline_single_tracker_owner_tests.moc"
