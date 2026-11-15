// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_local_diversification.hpp"
#include "azureus_remote_diversification.hpp"
#include "storage_diversification.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <algorithm>
using namespace biglybtc;
namespace {
void write(const QString& name,const QByteArray& bytes){QFile file(name);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()){throw std::runtime_error("Generated local checkpoint");}}
QByteArray read(const QString& name){QFile file(name);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated local checkpoint read");}return file.readAll();}
QByteArray localRow(char key,int type,int expiry,bool extra=false){return "d3:expi"+QByteArray::number(expiry)+"e3:key20:"+QByteArray(20,key)+(extra?"4:note4:keep":"")+"4:typei"+QByteArray::number(type)+"ee";}
QByteArray remoteRow(){return "d3:expi900000e3:fpoli2ei7ee3:key20:bbbbbbbbbbbbbbbbbbbb4:typei3ee";}
QByteArray map(QByteArray local,QByteArray remote=remoteRow()){return "d5:locall"+local+"e6:remotel"+remote+"e7:unknown4:keepe";}
namespace policy=services::azureus_storage_stats;
struct Clock final:policy::DiversificationClock{std::int64_t now=1000;unsigned calls=0;std::int64_t nowMilliseconds()noexcept override{++calls;return now;}};
struct Input final:policy::DiversificationControl{unsigned calls=0;std::expected<std::int64_t,policy::Error> expiryJitterMilliseconds()noexcept override{++calls;return 7;}std::int64_t suspendedUntilMilliseconds()const noexcept override{return 0;}};
}
class AzureusLocalDiversificationTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalCheckpointAndLazyExpiryFixture(){
  QFile file(QFINDTESTDATA("fixtures/azureus-local-diversification-original.bencode"));QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);const auto* cases=doc->find("cases");QVERIFY(cases);QCOMPARE(cases->children.size(),std::size_t{18});Clock clock;clock.now=doc->find("comparison_now")->integer;
  for(const auto& row:cases->children){const auto* before=row.find("before"),*after=row.find("after");QVERIFY(before&&after);auto restored=policy::DiversifyingKey::restore({std::int8_t(before->find("type")->integer),before->find("exp")->integer});QVERIFY(restored);const auto state=restored->checkpoint();QCOMPARE(state.type,std::int8_t(before->find("type")->integer));QCOMPARE(state.expiry,before->find("exp")->integer);auto stats=restored->statistics(clock);QCOMPARE(policy::encode(stats.value),std::string(row.find("stats")->bytes));QCOMPARE(restored->checkpoint().type,std::int8_t(after->find("type")->integer));QCOMPARE(restored->checkpoint().expiry,after->find("exp")->integer);QCOMPARE(row.find("writes")->integer,std::int64_t(stats.transition==policy::PolicyTransition::Expired?1:0));}
 }
 void restoreKeepsOnlyOriginalCheckpointAndConsumesNoEntropyOrClock(){
  Clock clock;Input input;auto live=policy::DiversifyingKey::create(clock,input);QVERIFY(live);QCOMPARE(live->changed(65,32769,clock,input),policy::PolicyTransition::Size);const auto calls=clock.calls,draws=input.calls;const auto snapshot=live->checkpoint();QCOMPARE(clock.calls,calls);auto restored=policy::DiversifyingKey::restore(snapshot);QVERIFY(restored);QCOMPARE(clock.calls,calls);QCOMPARE(input.calls,draws);auto stats=restored->statistics(clock);QCOMPARE(stats.value.entries,0);QCOMPARE(stats.value.bytes,0);QCOMPARE(stats.value.readsPerMinute,0);QCOMPARE(stats.value.diversification,std::int8_t{3});clock.now=snapshot.expiry+1;QCOMPARE(restored->statistics(clock).transition,policy::PolicyTransition::Expired);QCOMPARE(restored->checkpoint().type,std::int8_t{1});QCOMPARE(restored->checkpoint().expiry,snapshot.expiry);QVERIFY(!policy::DiversifyingKey::restore({4,snapshot.expiry}));
 }
 void localAndRemoteMergesRetainEachOtherAndUnknownFields(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,3);QVERIFY(versions);const auto base=root.path()+"/dht/net3/";write(base+"diverse.dat",map(localRow('a',3,900000,true)));auto before=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(before);auto remote=core::encodeValue(*before->find("remote"));QVERIFY(remote);auto locals=storage::readAzureusLocalDiversifications(**versions,1000);QVERIFY(locals&&locals->size()==1);auto a=(*locals)[0];a.type=2;a.expiry=1200000;auto c=a;c.key.fill('c');c.type=3;const std::array updates{a,c};QVERIFY(storage::saveAzureusLocalDiversifications(**versions,updates));auto after=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(after);auto remoteAfter=core::encodeValue(*after->find("remote"));QVERIFY(remoteAfter);QCOMPARE(*remoteAfter,*remote);QCOMPARE(after->find("unknown")->bytes,std::string_view("keep"));QCOMPARE(after->find("local")->children[0].find("note")->bytes,std::string_view("keep"));auto loaded=storage::readAzureusLocalDiversifications(**versions,1000);QVERIFY(loaded&&loaded->size()==2);QCOMPARE((*loaded)[0],a);QCOMPARE((*loaded)[1],c);
  auto remoteLoaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(remoteLoaded&&remoteLoaded->size()==1);auto b=(*remoteLoaded)[0];b.expiry=1300000;const std::array ru{b};const auto localBytes=core::encodeValue(*after->find("local"));QVERIFY(localBytes);QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,ru));after=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(after);auto localAfter=core::encodeValue(*after->find("local"));QVERIFY(localAfter);QCOMPARE(*localAfter,*localBytes);auto other=storage::AzureusValueVersions::open(*lease,0);QVERIFY(other);auto isolated=storage::readAzureusLocalDiversifications(**other,1000);QVERIFY(isolated&&isolated->empty());QVERIFY(!QFile::exists(base+"diverse.saving"));
 }
 void expiryDuplicatesAndClearMatchOriginalLocalState(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";write(base+"diverse.dat",map(localRow('a',3,900000)+localRow('a',2,800000)+localRow('a',3,10)+"4:keep"));auto locals=storage::readAzureusLocalDiversifications(**versions,1000);QVERIFY(locals&&locals->size()==1);QCOMPARE((*locals)[0].type,std::int8_t{2});auto atExpiry=storage::readAzureusLocalDiversifications(**versions,800000);QVERIFY(atExpiry&&atExpiry->size()==1);QCOMPARE((*atExpiry)[0].type,std::int8_t{3});auto clear=(*locals)[0];clear.type=1;const std::array updates{clear};QVERIFY(storage::saveAzureusLocalDiversifications(**versions,updates));locals=storage::readAzureusLocalDiversifications(**versions,1000);QVERIFY(locals&&locals->empty());auto doc=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(doc);QCOMPARE(doc->find("local")->children.size(),std::size_t{1});QCOMPARE(doc->find("local")->children[0].bytes,std::string_view("keep"));QCOMPARE(doc->find("remote")->children.size(),std::size_t{1});
 }
 void pendingMalformedAndDuplicateUpdatesNeverReplaceOriginalBytes(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";const auto raw=map(localRow('a',3,900000)+"d3:key20:aaaaaaaaaaaaaaaaaaaa4:type3:bade");write(base+"diverse.dat",raw);core::AzureusLocalDiversification item;item.key.fill('a');item.type=2;item.expiry=1200000;const std::array update{item};QVERIFY(!storage::saveAzureusLocalDiversifications(**versions,update));QCOMPARE(read(base+"diverse.dat"),raw);QVERIFY(!QFile::exists(base+"diverse.saving"));write(base+"diverse.dat",map(localRow('a',3,900000)));write(base+"diverse.saving","de");const auto before=read(base+"diverse.dat");QVERIFY(!storage::saveAzureusLocalDiversifications(**versions,update));QCOMPARE(read(base+"diverse.dat"),before);QCOMPARE(read(base+"diverse.saving"),QByteArray("de"));QVERIFY(QFile::remove(base+"diverse.saving"));const std::array duplicate{item,item};QVERIFY(!storage::saveAzureusLocalDiversifications(**versions,duplicate));QCOMPARE(read(base+"diverse.dat"),before);
 }
 void clearingAbsentLocalCheckpointIsNoOp(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);core::AzureusLocalDiversification item;item.key.fill('z');const std::array update{item};QVERIFY(storage::saveAzureusLocalDiversifications(**versions,update));QVERIFY(!QFile::exists(root.path()+"/dht/diverse.dat"));
 }
};
QTEST_GUILESS_MAIN(AzureusLocalDiversificationTests)
#include "azureus_local_diversification_tests.moc"
