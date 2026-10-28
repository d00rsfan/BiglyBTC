#include "network_policy.hpp"
#include "queued_mainline_scrape.hpp"
#include "owned_mainline_krpc.hpp"
#include "persistent_session.hpp"
#include <QElapsedTimer>
#include <QtTest>
#include <chrono>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt=libtorrent;
using test::mainline::OwnedKrpc;

void prepareProfile(test::OwnedEngineFixture& fixture,bool complete){
  auto source=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(source.has_value(),"Cannot parse owned active metadata");
  const auto raw=core::encodeValue(*source->find("attributes"));test::require(raw.has_value(),"Cannot encode owned attributes");
  auto attrs=core::Document::parse(*raw);test::require(attrs.has_value(),"Cannot parse owned attributes");
  test::require(attrs->replaceValue("pluginoptions","d5:mldhtd14:enableannouncei0eee").has_value(),"Cannot set owned zero-announce policy");
  test::require(source->replaceValue("attributes",attrs->serialize()).has_value(),"Cannot update owned attributes");
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
} // namespace

class MainlineActiveScrapeOwnerTests final:public QObject {
  Q_OBJECT
private slots:
  void genuineDownloadingAndSeedingUseScrapeOnlyWithoutGrantingPeerDiscovery(){
    test::OwnedEngineFixture downloading(false),seeding(false);prepareProfile(downloading,false);prepareProfile(seeding,true);
    const auto downInfo=rawInfo(downloading.stateFile),seedInfo=rawInfo(seeding.stateFile);
    const auto downPayload=test::read(downloading.payload+"/sample.bin"),seedPayload=test::read(seeding.payload+"/sample.bin");
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc downWire(peer.serverPort()),seedWire(peer.serverPort());
    const auto downHash=lt::sha1_hash(reinterpret_cast<const char*>(downloading.hash->bytes().data()));
    const auto seedHash=lt::sha1_hash(reinterpret_cast<const char*>(seeding.hash->bytes().data()));
    // Same metadata hash lives in different isolated owner/profile cohorts.
    downWire.allowWithoutPeers(downHash);seedWire.allowWithoutPeers(seedHash);downWire.scrapeNoSeeds(0);seedWire.scrapeNoSeeds(1);
    test::write(downloading.profile+"/plugins/mldht/IPv4-table.cache",cache(downWire.port()));
    test::write(seeding.profile+"/plugins/mldht/IPv4-table.cache",cache(seedWire.port()));
    auto downLease=storage::ProfileLease::acquire(downloading.native()),seedLease=storage::ProfileLease::acquire(seeding.native());QVERIFY(downLease);QVERIFY(seedLease);
    QElapsedTimer elapsed;elapsed.start();
    auto downCreated=engine::PersistentSession::create({7801,7802,7803}),seedCreated=engine::PersistentSession::create({7811,7812,7813});QVERIFY(downCreated);QVERIFY(seedCreated);
    auto down=std::move(*downCreated),seed=std::move(*seedCreated);const auto downPolicy=policy(),seedPolicy=policy();
    QVERIFY(down->configureNetwork(downPolicy));QVERIFY(seed->configureNetwork(seedPolicy));
    QVERIFY(down->configureMainlineBootstrap({}));QVERIFY(seed->configureMainlineBootstrap({}));
    QVERIFY(down->startIdle(*downLease));QVERIFY(seed->startIdle(*seedLease));
    QVERIFY(down->startManaged(*downLease,{.hash=*downloading.hash,.queued=false}));
    QVERIFY(seed->startManaged(*seedLease,{.hash=*seeding.hash,.queued=false}));
    QString failure;
    const auto pump=[&]{
      for(auto* owner:{down.get(),seed.get()}){
        if(!owner->poll()){failure="Owned active poll failed";return false;}
        const auto status=owner->mainlineDiscovery();
        if(status.failure||status.queuedScrapeFailure){failure=QString::fromStdString(status.failure?status.failure->message:status.queuedScrapeFailure->message);return false;}
        for(const auto& value:owner->managedDownloads()){if(value.failure){failure=QString::fromStdString(value.failure->message);return false;}}
      }
      if(!downWire.failure.isEmpty()||!seedWire.failure.isEmpty()){failure=downWire.failure+seedWire.failure;return false;}
      return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(([&]{
      if(!pump()){return true;}const auto a=down->managedDownloads(),b=seed->managedDownloads();
      return a.size()==1&&b.size()==1&&a[0].checked&&b[0].checked
          &&a[0].phase==engine::ManagedPhase::Downloading&&b[0].phase==engine::ManagedPhase::Seeding;
    }()),15000);
    QVERIFY2(failure.isEmpty(),qPrintable(failure));
    const auto downAdmission=down->managedDownloads()[0].admission,seedAdmission=seed->managedDownloads()[0].admission;
    QVERIFY(downAdmission);QVERIFY(seedAdmission);QVERIFY(!down->managedDownloads()[0].complete);QVERIFY(seed->managedDownloads()[0].complete);
    // Saved50/60 are not saved queued75; only actual token-bound admissions
    // can supply this branch. No configured tracker/error state is invented.
    auto downSaved=engine::readSavedMainlineScrapeTargets(*downLease),seedSaved=engine::readSavedMainlineScrapeTargets(*seedLease);QVERIFY(downSaved);QVERIFY(seedSaved);QVERIFY(downSaved->empty());QVERIFY(seedSaved->empty());
    QTRY_VERIFY_WITH_TIMEOUT(([&]{return !pump()||(!down->mainlineScrapeEstimates().empty()&&!seed->mainlineScrapeEstimates().empty());}()),135000);
    QVERIFY2(failure.isEmpty(),qPrintable(failure));
    QVERIFY2(elapsed.elapsed()>=99999,"The production startup barrier was bypassed");
    const auto downEstimate=down->mainlineScrapeEstimates(),seedEstimate=seed->mainlineScrapeEstimates();QCOMPARE(downEstimate.size(),std::size_t{1});QCOMPARE(seedEstimate.size(),std::size_t{1});
    QCOMPARE(downEstimate[0].hash,*downloading.hash);QCOMPARE(seedEstimate[0].hash,*seeding.hash);
    for(const auto& value:{downEstimate[0],seedEstimate[0]}){QCOMPARE(value.counts.responses,std::size_t{1});QCOMPARE(value.counts.seeds,1);QCOMPARE(value.counts.peers,1);QVERIFY(value.startedWallMillis>0);}
    QCOMPARE(downWire.scrapes(),1);QCOMPARE(seedWire.scrapes(),1);QCOMPARE(downWire.gets(downHash),1);QCOMPARE(seedWire.gets(seedHash),1);
    for(auto* owner:{down.get(),seed.get()}){const auto observed=owner->mainlineDiscovery();QCOMPARE(observed.lookups,std::uint64_t{0});QCOMPARE(observed.announces,std::uint64_t{0});QCOMPARE(observed.deliveredPeers,std::uint64_t{0});}
    QCOMPARE(down->managedDownloads()[0].admission,downAdmission);QCOMPARE(seed->managedDownloads()[0].admission,seedAdmission);
    QCOMPARE(down->managedDownloads()[0].received,std::int64_t{0});QCOMPARE(seed->managedDownloads()[0].received,std::int64_t{0});QVERIFY(!peer.hasPendingConnections());
    QVERIFY(down->pauseManaged(*downloading.hash,true));QVERIFY(down->mainlineScrapeEstimates().empty());
    QVERIFY(seed->stopManaged(*seeding.hash));QVERIFY(seed->mainlineScrapeEstimates().empty());
    const auto a=down->close(std::chrono::steady_clock::now()+10s),b=seed->close(std::chrono::steady_clock::now()+10s);
    QVERIFY(a.sessionJoined&&a.retirement.storageReleased&&!a.checkpointFailure);QVERIFY(b.sessionJoined&&b.retirement.storageReleased&&!b.checkpointFailure);
    QCOMPARE(test::read(downloading.payload+"/sample.bin"),downPayload);QCOMPARE(test::read(seeding.payload+"/sample.bin"),seedPayload);
    QCOMPARE(rawInfo(downloading.stateFile),downInfo);QCOMPARE(rawInfo(seeding.stateFile),seedInfo);QVERIFY(downloading.accountingIntact(false));QVERIFY(seeding.accountingIntact());
    qInfo("actual active-scrape admissions=%llu/%llu, actual requests=%d/%d, no discovery or announce",
        static_cast<unsigned long long>(downAdmission),static_cast<unsigned long long>(seedAdmission),downWire.scrapes(),seedWire.scrapes());
  }
};
QTEST_GUILESS_MAIN(MainlineActiveScrapeOwnerTests)
#include "mainline_active_scrape_owner_tests.moc"
