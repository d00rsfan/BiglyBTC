#include "mainline_dht.hpp"
#include "mainline_identity.hpp"
#include "owned_engine_fixture.hpp"

#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtTest>
#include <array>
#include <map>
#include <sys/stat.h>
#include <string_view>

namespace {
using namespace biglybtc;
namespace lt=libtorrent;
QByteArray str(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
QByteArray dict(const std::map<QByteArray,QByteArray>& values){
  QByteArray result("d");for(const auto& [key,value]:values){result+=str(key);result+=value;}return result+'e';
}
struct Source {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-original-wire-identity-XXXXXX")};
  QString profile,leaf;QByteArray bytes;struct stat original{};
  std::shared_ptr<storage::ProfileLease> lease;
  std::shared_ptr<storage::OriginalMainlineIdentity> identity;
  explicit Source(const QByteArray& key){
    test::require(root.isValid(),"Owned original-ID directory");profile=root.filePath("profile");
    test::require(QDir().mkpath(profile+"/plugins/mldht"),"Owned original-ID parent");
    leaf=profile+"/plugins/mldht/baseID.config";bytes="# retained original\r\n"+key.toHex().toUpper()+"\r\n# future\n";
    QFile file(leaf);test::require(file.open(QIODevice::WriteOnly),"Owned original-ID file");
    test::require(file.write(bytes)==bytes.size()&&file.flush(),"Owned original-ID publish");file.close();
    test::require(::lstat(QFile::encodeName(leaf).constData(),&original)==0,"Owned original-ID stat");
    auto held=storage::ProfileLease::acquire(profile.toStdString());test::require(held.has_value(),"Owned original-ID lease");
    lease=std::make_shared<storage::ProfileLease>(std::move(*held));
    auto observed=storage::OriginalMainlineIdentity::load(*lease,"mldht");test::require(observed.has_value(),"Actual original-ID reader");
    identity=std::make_shared<storage::OriginalMainlineIdentity>(std::move(*observed));
  }
  bool unchanged()const{
    struct stat after{};if(::lstat(QFile::encodeName(leaf).constData(),&after)){return false;}
    QFile file(leaf);if(!file.open(QIODevice::ReadOnly)){return false;}
    return file.readAll()==bytes&&file.error()==QFileDevice::NoError&&after.st_dev==original.st_dev
        &&after.st_ino==original.st_ino&&after.st_mode==original.st_mode&&after.st_size==original.st_size
        &&after.st_mtim.tv_sec==original.st_mtim.tv_sec&&after.st_mtim.tv_nsec==original.st_mtim.tv_nsec
        &&after.st_ctim.tv_sec==original.st_ctim.tv_sec&&after.st_ctim.tv_nsec==original.st_ctim.tv_nsec;
  }
};
// Exact expected ORIGINAL local ID, including zero, is checked on each real
// datagram. Remote replies retain a distinct nonzero ID. No public addresses.
struct Krpc final : QObject {
  QUdpSocket socket;QByteArray expected;unsigned short sourcePort=0;int queries=0,gets=0,bootstraps=0;QString failure;
  explicit Krpc(bool v6,QByteArray key):expected(std::move(key)){
    test::require(socket.bind(v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Owned exact-ID KRPC bind");
    QObject::connect(&socket,&QUdpSocket::readyRead,this,[this]{
      while(socket.hasPendingDatagrams()){
        const auto packet=socket.receiveDatagram();auto parsed=core::Document::parse(packet.data().toStdString());
        if(!parsed){failure="Malformed owned KRPC";continue;}
        const auto* arguments=parsed->find("a");const auto* query=parsed->find("q");const auto* transaction=parsed->find("t");
        const auto* id=arguments?arguments->find("id"):nullptr;
        if(!id||id->type!=core::Node::Type::Bytes||QByteArray(id->bytes.data(),static_cast<qsizetype>(id->bytes.size()))!=expected
            ||!query||query->type!=core::Node::Type::Bytes||!transaction||transaction->type!=core::Node::Type::Bytes
            ||packet.senderPort()!=sourcePort||!packet.senderAddress().isLoopback()){
          failure="Actual KRPC source or exact original local ID differs";continue;
        }
        if(query->bytes!="ping"&&query->bytes!="find_node"&&query->bytes!="get_peers"){
          failure="Unexpected query in exact-ID lookup fixture";continue;
        }
        ++queries;std::map<QByteArray,QByteArray> response{{"id",str(QByteArray(20,'K'))},{"nodes",str({})}};
        if(query->bytes=="get_peers"){
          const auto* requested=arguments->find("info_hash");
          const auto* bootstrap=arguments->find("bs");
          if(bootstrap){
            // LT refresh.cpp bootstrap::invoke uses the current local ID with
            // only its final8 bytes secretized, explicitly marked bs=1. This
            // is routing bootstrap, never the caller's requested info hash.
            if(bootstrap->type!=core::Node::Type::Integer||bootstrap->integer!=1
                ||!requested||requested->type!=core::Node::Type::Bytes||requested->bytes.size()!=20
                ||requested->bytes.substr(0,12)!=std::string_view(expected.constData(),12)
                ||arguments->find("scrape")||arguments->find("noseed")){
              failure="Exact-ID bootstrap used the wrong marker or local-ID prefix";continue;
            }
            ++bootstraps; // Nodes-only response below: no peers, token, gets or scrape credit.
          }else{
            if(!requested||requested->type!=core::Node::Type::Bytes||requested->bytes!=std::string(20,'h')){
              failure="Exact-ID lookup used the wrong info hash";continue;
            }
          ++gets;QByteArray compact;if(packet.senderAddress().protocol()==QAbstractSocket::IPv4Protocol){compact=QByteArray::fromHex("7f000001");}
          else{compact=QByteArray(15,'\0')+'\1';}
          compact+=QByteArray::fromHex("3039");response["token"]=str("owned");response["values"]='l'+str(compact)+'e';
          }
        }
        const auto result=dict({{"r",dict(response)},{"t",str(QByteArray(transaction->bytes.data(),static_cast<qsizetype>(transaction->bytes.size())))},{"y",str("r")}});
        if(socket.writeDatagram(result,packet.senderAddress(),packet.senderPort())!=result.size()){failure="Owned exact-ID reply failed";}
      }
    });
  }
};
unsigned short availablePort(bool v6){QUdpSocket socket;test::require(socket.bind(v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Owned DHT port");return socket.localPort();}
engine::NetworkPolicy policy(bool v6){
  engine::NetworkPolicy result;result.mode=engine::NetworkMode::Normal;result.dht=true;result.ipv6=v6;
  result.mainlineIpv4=!v6;result.mainlineIpv6=v6;result.bindAddresses={v6?"::1":"127.0.0.1"};return result;
}
engine::MainlineDhtOptions options(Source& source,bool v6){
  engine::MainlineDhtOptions result;result.port=availablePort(v6);result.ipv4=!v6;result.ipv6=v6;
  result.bootstrapNodes.clear();result.originalIdentity=source.identity;result.originalIdentityLease=source.lease;return result;
}
}
class OriginalMainlineWireTests final : public QObject {
  Q_OBJECT
private slots:
#if __has_include(<libtorrent/biglybtc_mainline_identity.hpp>)
  void exactOriginalWireIdentity_data(){
    QTest::addColumn<bool>("v6");QTest::addColumn<bool>("zero");
    QTest::newRow("v4-arbitrary")<<false<<false;QTest::newRow("v4-zero")<<false<<true;
    QTest::newRow("v6-arbitrary")<<true<<false;QTest::newRow("v6-zero")<<true<<true;
  }
  void exactOriginalWireIdentity(){
    QFETCH(bool,v6);QFETCH(bool,zero);const QByteArray key(20,zero?'\0':char(0x5a));Source source(key);Krpc wire(v6,key);
    auto opts=options(source,v6);wire.sourcePort=static_cast<unsigned short>(opts.port);
    opts.bootstrapNodes={{v6?"::1":"127.0.0.1",wire.socket.localPort()}};
    auto made=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY2(made,made?"":made.error().message.c_str());
    auto service=std::move(*made);const QByteArray rawHash(20,'h');const lt::sha1_hash hash(rawHash.constData());
    QVERIFY(service->lookup(hash));bool delivered=false;QString failure;
    auto pump=[&]{auto result=service->poll();if(!result){failure=QString::fromStdString(result.error().message);return false;}
      for(const auto& row:*result){if(row.hash==hash&&!row.peers.empty()){delivered=true;}}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(pump()&&delivered,15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY2(wire.failure.isEmpty(),qPrintable(wire.failure));
    QVERIFY(wire.queries>0);QVERIFY(wire.gets>0);QVERIFY(wire.bootstraps>0);const auto active=service->status();
    QVERIFY(active.originalIdentityRequested);QVERIFY(active.originalIdentityActive);QCOMPARE(active.originalIdentityAdoptions,std::uint64_t{1});
    QCOMPARE(active.listenPort,static_cast<unsigned short>(opts.port));QCOMPARE(active.lookups,std::uint64_t{1});
    QCOMPARE(active.announces,std::uint64_t{0});QVERIFY(source.unchanged());
    auto checkpoint=service->checkpoint();QVERIFY(checkpoint);auto document=core::Document::parse(*checkpoint);QVERIFY(document);
    const auto* dht=document->find("dht state");QVERIFY(dht);const auto* ids=dht->find("node-id");QVERIFY(!ids||ids->children.empty());
    auto duplicate=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY(!duplicate);QCOMPARE(duplicate.error().code,storage::ErrorCode::Busy);
    QVERIFY(service->close());const auto closed=service->status();QVERIFY(closed.originalIdentityJoined);QVERIFY(!closed.originalIdentityActive);
    QCOMPARE(closed.originalIdentityRetirements,std::uint64_t{1});QVERIFY(source.unchanged());
    // Actual join releases the original family slot. The same retained source
    // can be freshly installed; this is not a reader-only reservation claim.
    auto next=engine::MainlineDhtService::create(policy(v6),opts);QVERIFY(next);QVERIFY((*next)->close());QVERIFY(source.unchanged());
  }
  void changedHeldSourceRetiresActualOriginalNodeWithoutFallback(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);
    auto made=engine::MainlineDhtService::create(policy(false),opts);QVERIFY2(made,made?"":made.error().message.c_str());
    QString failure;auto listening=[&]{auto observed=(*made)->poll();
      if(!observed){failure=QString::fromStdString(observed.error().message);return false;}
      return (*made)->status().originalIdentityActive;};
    QTRY_VERIFY_WITH_TIMEOUT(listening(),5000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    QFile original(source.leaf);QVERIFY(original.rename(source.leaf+".old"));QFile replacement(source.leaf);
    QVERIFY(replacement.open(QIODevice::WriteOnly));QCOMPARE(replacement.write(source.bytes),source.bytes.size());replacement.close();
    const auto denied=(*made)->poll();QVERIFY(!denied);const auto stopped=(*made)->status();
    QCOMPARE(stopped.phase,engine::MainlineDhtPhase::Failed);QVERIFY(stopped.originalIdentityJoined);
    QVERIFY(!stopped.originalIdentityActive);QCOMPARE(stopped.originalIdentityAdoptions,std::uint64_t{1});
    QCOMPARE(stopped.originalIdentityRetirements,std::uint64_t{1});
  }
  void currentSourceAndSingleSocketAreRequiredBeforeConstruction(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);auto dual=policy(false);dual.ipv6=true;dual.mainlineIpv6=true;
    dual.bindAddresses.push_back("::1");opts.ipv6=true;auto ambiguous=engine::MainlineDhtService::create(dual,opts);
    QVERIFY(!ambiguous);QCOMPARE(ambiguous.error().code,storage::ErrorCode::Unsupported);opts.ipv6=false;
    QFile old(source.leaf);QVERIFY(old.rename(source.leaf+".original"));QFile replacement(source.leaf);
    QVERIFY(replacement.open(QIODevice::WriteOnly));QCOMPARE(replacement.write(source.bytes),source.bytes.size());replacement.close();
    auto stale=engine::MainlineDhtService::create(policy(false),opts);QVERIFY(!stale);QCOMPARE(stale.error().code,storage::ErrorCode::Conflict);
  }
#else
  void stockSdkRefusesRequestedOriginalPolicyExplicitly(){
    Source source(QByteArray(20,'r'));auto opts=options(source,false);
    auto result=engine::MainlineDhtService::create(policy(false),opts);QVERIFY(!result);
    QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);QVERIFY(source.unchanged());
  }
#endif
};
QTEST_GUILESS_MAIN(OriginalMainlineWireTests)
#include "mainline_original_identity_tests.moc"
