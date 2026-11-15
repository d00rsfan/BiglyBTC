// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_identity_profile.hpp"
#include "azureus_recent_address_area.hpp"
#include "key_block_fixture.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QUdpSocket>
using namespace biglybtc;namespace block=services::azureus_key_block;
namespace {
void write(const QString& path,std::string_view bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size())){throw std::runtime_error("Generated identity checkpoint");}}
std::string read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated identity checkpoint read");}return f.readAll().toStdString();}
std::string addressMap(std::string_view value){return "d11:most_recent"+std::to_string(value.size())+":"+std::string(value)+"e";}
struct Profile {
 QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::shared_ptr<storage::AzureusValueVersions> versions;generated_key_blocks::Data data;std::vector<block::Record> records;QUdpSocket socket;std::int64_t now=1000000000000;
 Profile(const char* bind,int network=0){auto acquired=storage::ProfileLease::acquire(root.path().toStdString());if(!acquired){throw std::runtime_error("Generated identity lease");}lease=std::move(*acquired);auto owner=storage::AzureusValueVersions::open(*lease,network);if(!owner){throw std::runtime_error("Generated identity versions");}versions=std::move(*owner);if(!socket.bind(QHostAddress(QString::fromLatin1(bind)),0)){throw std::runtime_error("Actual generated identity socket");}auto state=data.state();if(!state->admit(data.request,data.signature,std::string(20,'s'),now)){throw std::runtime_error("Generated signed identity block");}records=state->records();auto original=core::Document::parse("d6:opaque4:keepe");auto encoded=block::encodeSaved(original->root(),records);if(!encoded){throw std::runtime_error("Generated signed identity map");}write(base()+"block.dat",*encoded);}
 QString base()const{return root.path()+(versions->network()?"/dht/net"+QString::number(versions->network())+"/":"/dht/");}
};
}
class AzureusIdentityStartupTests final:public QObject{Q_OBJECT
private slots:
 void unchangedHostAndExpandedIpv6RetainDirectSignedBlocks_data(){QTest::addColumn<QByteArray>("bind");QTest::addColumn<QByteArray>("previous");QTest::addColumn<int>("network");QTest::newRow("ipv4")<<QByteArray("127.181.91.1")<<QByteArray("127.181.91.1")<<0;QTest::newRow("ipv6-java-expanded")<<QByteArray("::1")<<QByteArray("0:0:0:0:0:0:0:1")<<3;}
 void unchangedHostAndExpandedIpv6RetainDirectSignedBlocks(){
  QFETCH(QByteArray,bind);QFETCH(QByteArray,previous);QFETCH(int,network);Profile f(bind.constData(),network);write(f.base()+"addresses.dat",addressMap(previous.toStdString()));const auto bytes=read(f.base()+"block.dat");auto area=storage::readAzureusKeyBlockArea(*f.versions);QVERIFY(area);auto prepared=app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier());QVERIFY(prepared);QVERIFY(!prepared->addressChanged&&!prepared->directBlocksPurged&&prepared->addressRecorded&&!prepared->warning);QVERIFY(prepared->blocks->blocked(f.data.request.substr(8)));QCOMPARE(read(f.base()+"block.dat"),bytes);auto address=core::Document::parse(read(f.base()+"addresses.dat"));QVERIFY(address);QCOMPARE(address->find("most_recent")->bytes,previous.toStdString());QVERIFY(address->find(previous.toStdString()));QCOMPARE(address->find(previous.toStdString())->integer,f.now);
 }
 void changedBoundHostPurgesBlocksBeforeUpdatingHistory(){
  Profile f("127.182.91.1");write(f.base()+"addresses.dat",addressMap("127.181.91.1"));auto area=storage::readAzureusKeyBlockArea(*f.versions);QVERIFY(area);auto prepared=app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier());QVERIFY(prepared);QVERIFY(prepared->addressChanged&&prepared->directBlocksPurged&&prepared->addressRecorded);QVERIFY(!prepared->blocks->blocked(f.data.request.substr(8)));auto saved=core::Document::parse(read(f.base()+"block.dat"));QVERIFY(saved);QCOMPARE(saved->find("opaque")->bytes,std::string_view("keep"));QVERIFY(saved->find("entries")->children.empty());auto address=core::Document::parse(read(f.base()+"addresses.dat"));QVERIFY(address);QCOMPARE(address->find("most_recent")->bytes,std::string_view("127.182.91.1"));QCOMPARE(address->find("127.181.91.1")->integer,f.now);QCOMPARE(address->find("127.182.91.1")->integer,f.now);
 }
 void interruptedAddressPublicationLeavesTheBlockPurgeDurable(){
  Profile f("127.183.91.1");const auto before=addressMap("127.181.91.1");write(f.base()+"addresses.dat",before);write(f.base()+"addresses.saving","de");auto area=storage::readAzureusKeyBlockArea(*f.versions);QVERIFY(area);auto prepared=app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier());QVERIFY(prepared&&prepared->directBlocksPurged&&!prepared->addressRecorded&&prepared->warning);QCOMPARE(read(f.base()+"addresses.dat"),before);QCOMPARE(read(f.base()+"addresses.saving"),std::string("de"));auto blocks=core::Document::parse(read(f.base()+"block.dat"));QVERIFY(blocks&&blocks->find("entries")->children.empty());
 }
 void staleBlockPreimageCannotPublishANewAddress(){
  Profile f("127.184.91.1");const auto before=addressMap("127.181.91.1");write(f.base()+"addresses.dat",before);auto area=storage::readAzureusKeyBlockArea(*f.versions);QVERIFY(area);write(f.base()+"block.dat","de");auto prepared=app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier());QVERIFY(!prepared);QCOMPARE(read(f.base()+"addresses.dat"),before);QCOMPARE(read(f.base()+"block.dat"),std::string("de"));
 }
 void unreadableOptionalHistoryIsRetainedAndCancelledWorkWritesNothing(){
  Profile f("127.185.91.1");write(f.base()+"addresses.dat","unreadable");const auto before=read(f.base()+"block.dat");auto area=storage::readAzureusKeyBlockArea(*f.versions);QVERIFY(area);auto prepared=app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier());QVERIFY(prepared&&prepared->warning&&!prepared->addressRecorded);QCOMPARE(read(f.base()+"addresses.dat"),std::string("unreadable"));QCOMPARE(read(f.base()+"block.dat"),before);std::stop_source stop;stop.request_stop();QVERIFY(!app::prepareAzureusIdentityProfile(*f.versions,*area,f.records,f.socket.localAddress(),f.now,f.data.verifier(),stop.get_token()));QCOMPARE(read(f.base()+"block.dat"),before);
 }
};
QTEST_GUILESS_MAIN(AzureusIdentityStartupTests)
#include "azureus_identity_startup_tests.moc"
