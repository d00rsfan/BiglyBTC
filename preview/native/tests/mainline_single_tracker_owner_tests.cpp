#include "network_policy.hpp"
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
  engine::NetworkPolicy result;result.mode=engine::NetworkMode::Normal;result.dht=true;result.ipv6=false;
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
} // namespace

class MainlineSingleTrackerOwnerTests final:public QObject {
  Q_OBJECT
private slots:
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
    auto a=engine::PersistentSession::create({7901,7902,7903}),b=engine::PersistentSession::create({7911,7912,7913});QVERIFY(a);QVERIFY(b);
    auto bad=std::move(*a),good=std::move(*b);const auto badPolicy=policy(),goodPolicy=policy();
    failedTracker.peerPort(badPolicy.listenPort);healthyTracker.peerPort(goodPolicy.listenPort);
    QVERIFY(bad->configureNetwork(badPolicy));QVERIFY(good->configureNetwork(goodPolicy));
    QVERIFY(bad->configureMainlineBootstrap({}));QVERIFY(good->configureMainlineBootstrap({}));
    QElapsedTimer elapsed;elapsed.start();QVERIFY(bad->startIdle(*failedLease));QVERIFY(good->startIdle(*healthyLease));
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
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto status=good->managedDownloads();
      if(status.size()!=1||!status[0].queueSourceFacts||!status[0].queueSourceFacts->singleTrackerResponse){return false;}
      const auto& response=status[0].queueSourceFacts->singleTrackerResponse->response;
      return response.type==core::QueueOriginalScrapeType::Success&&response.content&&response.content->seeds==5&&response.content->peers==6;
    }()),5000);
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
    const auto ca=bad->close(std::chrono::steady_clock::now()+10s),cb=good->close(std::chrono::steady_clock::now()+10s);
    QVERIFY(ca.sessionJoined&&ca.retirement.storageReleased&&!ca.checkpointFailure);QVERIFY(cb.sessionJoined&&cb.retirement.storageReleased&&!cb.checkpointFailure);
    QCOMPARE(test::read(failed.payload+"/sample.bin"),failedPayload);QCOMPARE(test::read(healthy.payload+"/sample.bin"),healthyPayload);
    QCOMPARE(rawInfo(failed.stateFile),failedInfo);QCOMPARE(rawInfo(healthy.stateFile),healthyInfo);QVERIFY(failed.accountingIntact(false));QVERIFY(healthy.accountingIntact(false));
    qInfo("actual configured UDP: failed announces=%d scrapes=%d, healthy announces=%d scrapes=%d, DHT=%d/%d",
        failedTracker.announces,failedTracker.scrapes,healthyTracker.announces,healthyTracker.scrapes,failedWire.scrapes(),healthyWire.scrapes());
  }
};
QTEST_GUILESS_MAIN(MainlineSingleTrackerOwnerTests)
#include "mainline_single_tracker_owner_tests.moc"
