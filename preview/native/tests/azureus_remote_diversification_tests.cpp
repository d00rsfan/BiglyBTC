// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_remote_diversification.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QSet>
#include <unistd.h>
using namespace biglybtc;
namespace {
void write(const QString& path,const QByteArray& data){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()){throw std::runtime_error("Generated diversification fixture");}}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Read generated fixture");}return file.readAll();}
}
class AzureusRemoteDiversificationTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalSerialisationAndKeyPolicyFixture(){
  const auto path=QFINDTESTDATA("fixtures/azureus-remote-diversification-original.bencode");QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);const auto* rows=doc->find("cases");QVERIFY(rows);QCOMPARE(rows->children.size(),std::size_t{32});
  for(const auto& row:rows->children){const auto* source=row.find("record");QVERIFY(source);core::AzureusRemoteDiversification r;const auto key=source->find("key")->bytes;std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,r.key.begin());r.type=std::int8_t(source->find("type")->integer);r.expiry=source->find("exp")->integer;const auto& fixed=source->find("fpo")->children;r.offsets={unsigned(fixed[0].integer),unsigned(fixed[1].integer)};QVERIFY(core::validAzureusDiversification(r));
   const QByteArray raw(key.data(),qsizetype(key.size()));const auto fragment=[&](unsigned offset){auto input=raw;input.append(char(offset));return QCryptographicHash::hash(input,QCryptographicHash::Sha1);};
   for(bool exhaustive:{false,true}){const auto* expected=row.find(exhaustive?"put_exhaustive":"put");auto offsets=core::azureusDiversificationOffsets(r,true,exhaustive,{0,1});QList<QByteArray> actual;for(auto offset:offsets){actual.push_back(fragment(offset));}if(exhaustive){actual.push_back(raw);}QCOMPARE(actual.size(),qsizetype(expected->children.size()));for(qsizetype i=0;i<actual.size();++i){QCOMPARE(actual[i],QByteArray(expected->children[std::size_t(i)].bytes.data(),20));}}
   for(bool exhaustive:{false,true}){const auto* expected=row.find(exhaustive?"get_exhaustive":"get");std::array<unsigned,2> random{0,1};QSet<QByteArray> seen;unsigned index=0;for(const auto& k:expected->children){QByteArray value(k.bytes.data(),20);QVERIFY(!seen.contains(value));seen.insert(value);bool found=false;for(unsigned n=0;n<10;++n){if(fragment(n)==value){found=true;if(index<2){random[index]=n;}break;}}QVERIFY(found);++index;}if(r.type==2){random[1]=(random[0]+1)%10;}auto offsets=core::azureusDiversificationOffsets(r,false,exhaustive,random);QCOMPARE(offsets.size(),expected->children.size());for(unsigned i=0;i<offsets.size();++i){QCOMPARE(fragment(offsets[i]),QByteArray(expected->children[i].bytes.data(),20));}}
  }QCOMPARE(doc->find("expired_removed")->integer,std::int64_t{1});QCOMPARE(doc->find("minted_ttl_and_offsets")->integer,std::int64_t{1});
 }
 void originalRemoteOffsetsSurviveMergeAndReopen(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,3);QVERIFY(versions);const auto base=root.path()+"/dht/net3/";
  const QByteArray fixture="d5:locall4:keepe6:remoteld3:expi900000e3:fpoli2ei7ee3:key20:aaaaaaaaaaaaaaaaaaaa4:typei3eee7:unknown4:keepe";write(base+"diverse.dat",fixture);
  auto loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded);QCOMPARE(loaded->size(),std::size_t{1});QCOMPARE((*loaded)[0].offsets,(std::array<unsigned,2>{2,7}));
  core::AzureusRemoteDiversification added;added.key.fill('b');added.type=2;added.expiry=800000;added.offsets={1,9};const std::array updates{added};QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,updates));auto merged=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(merged);QCOMPARE(merged->find("unknown")->bytes,std::string_view("keep"));QCOMPARE(merged->find("local")->children[0].bytes,std::string_view("keep"));QCOMPARE(merged->find("remote")->children.size(),std::size_t{2});QVERIFY(!QFile::exists(base+"diverse.saving"));
  loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded&&loaded->size()==2);auto reopened=storage::AzureusValueVersions::open(*lease,3);QVERIFY(reopened);auto again=storage::readAzureusRemoteDiversifications(**reopened,1000);QVERIFY(again);QCOMPARE(*again,*loaded);
  loaded=storage::readAzureusRemoteDiversifications(**versions,850000);QVERIFY(loaded&&loaded->size()==1);QCOMPARE((*loaded)[0].offsets,(std::array<unsigned,2>{2,7}));
 }
 void sharedLedgerKeepsOriginalTypeExpiryAndFixedPuts(){
  core::AzureusDiversificationLedger a,b;core::AzureusRemoteDiversification r;r.key.fill('g');r.type=3;r.expiry=10000;r.offsets={2,7};
  auto known=a.remember(r,1000);QVERIFY(known);QVERIFY(!a.persisted(r));QCOMPARE(a.dirty(1000,1).size(),std::size_t{1});QVERIFY(a.dirty(1000,0).empty());auto replacement=r;replacement.type=2;replacement.offsets={1,9};replacement.expiry=20000;QCOMPARE(a.remember(replacement,2000),known);QVERIFY(!a.markPersisted(replacement));QVERIFY(a.markPersisted(r));QVERIFY(a.dirty(1000,64).empty());
  const std::array original{r};QVERIFY(b.restore(original,1000));QVERIFY(b.persisted(r));QCOMPARE(core::azureusDiversificationOffsets(*a.find(r.key,1000),true,false,{0,1}),core::azureusDiversificationOffsets(*b.find(r.key,1000),true,false,{8,9}));
  QCOMPARE(core::azureusDiversificationOffsets(r,true,false,{0,1}),(std::vector<unsigned>{2,7}));QCOMPARE(core::azureusDiversificationOffsets(r,false,false,{0,1}),(std::vector<unsigned>{0,1}));QCOMPARE(core::azureusDiversificationOffsets(r,false,true,{0,1}).size(),std::size_t{10});
  auto frequency=r;frequency.type=2;QCOMPARE(core::azureusDiversificationOffsets(frequency,true,false,{0,1}).size(),std::size_t{10});QCOMPARE(core::azureusDiversificationOffsets(frequency,false,true,{8,9}),(std::vector<unsigned>{8}));
  QVERIFY(a.find(r.key,10000));QVERIFY(!a.find(r.key,10001));QCOMPARE(a.remember(replacement,10001),std::optional{replacement});QVERIFY(!a.persisted(replacement));QCOMPARE(b.find(r.key,1000),std::optional{r});
 }
 void duplicateOriginalRowsAndUnknownFieldsSurviveAnUpdate(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";
  const QByteArray first="d3:expi9000e3:fpoli1ei9ee3:key20:aaaaaaaaaaaaaaaaaaaa4:typei2ee";
  const QByteArray last="d3:expi8000e3:fpoli2ei7ee3:key20:aaaaaaaaaaaaaaaaaaaa7:private4:keep4:typei3ee";
  const QByteArray expired="d3:expi10e3:fpoli3ei8ee3:key20:aaaaaaaaaaaaaaaaaaaa4:typei2ee";
  write(base+"diverse.dat","d5:locall4:keepe6:remotel"+first+last+expired+"3:bade7:unknown4:keepe");
  auto loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded);QCOMPARE(loaded->size(),std::size_t{1});QCOMPARE((*loaded)[0].type,std::int8_t{3});QCOMPARE((*loaded)[0].offsets,(std::array<unsigned,2>{2,7}));
  // Patch the last recognised physical row, retain every other raw entry.
  auto updated=(*loaded)[0];updated.expiry=20000;const std::array updates{updated};QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,updates));auto doc=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(doc);const auto& rows=doc->find("remote")->children;QCOMPARE(rows.size(),std::size_t{4});QCOMPARE(rows[1].find("private")->bytes,std::string_view("keep"));QCOMPARE(rows[2].find("exp")->integer,std::int64_t{20000});QCOMPARE(rows[3].bytes,std::string_view("bad"));QCOMPARE(doc->find("unknown")->bytes,std::string_view("keep"));
  auto repeated=read(base+"diverse.dat");QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,updates));QCOMPARE(read(base+"diverse.dat"),repeated);
  const std::array duplicates{updated,updated};QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,duplicates));QCOMPARE(read(base+"diverse.dat"),repeated);
  auto other=storage::AzureusValueVersions::open(*lease,3);QVERIFY(other);auto empty=storage::readAzureusRemoteDiversifications(**other,1000);QVERIFY(empty&&empty->empty());
 }
 void matchedRecordPreservesOwnUnknownMap(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";
  write(base+"diverse.dat","d6:remoteld3:expi9000e3:fpoli2ei7ee3:key20:aaaaaaaaaaaaaaaaaaaa7:private4:keep4:typei3eeee");auto loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded&&loaded->size()==1);auto item=(*loaded)[0];item.expiry=12000;const std::array updates{item};QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,updates));auto doc=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(doc);const auto& row=doc->find("remote")->children[0];QCOMPARE(row.find("private")->bytes,std::string_view("keep"));QCOMPARE(row.find("exp")->integer,std::int64_t{12000});
 }
 void malformedCollidingRecordCannotClaimACanonicalPublication(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";
  const QByteArray raw="d6:remoteld3:expi9000e3:fpoli2ei7ee3:key20:aaaaaaaaaaaaaaaaaaaa4:typei3eed3:key20:aaaaaaaaaaaaaaaaaaaa4:type3:bad3:new4:keepeee";write(base+"diverse.dat",raw);auto loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded&&loaded->size()==1);auto updated=(*loaded)[0];updated.expiry=12000;const std::array updates{updated};QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,updates));QCOMPARE(read(base+"diverse.dat"),raw);QVERIFY(!QFile::exists(base+"diverse.saving"));
 }
 void pendingAndUnsafeSourcesDoNotOverwriteOriginalState(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";write(base+"diverse.dat","d6:remotelee");write(base+"diverse.saving","de");core::AzureusRemoteDiversification r;r.key.fill('c');r.type=3;r.expiry=900000;r.offsets={3,8};const std::array update{r};const auto before=read(base+"diverse.dat");QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,update));QCOMPARE(read(base+"diverse.dat"),before);QCOMPARE(read(base+"diverse.saving"),QByteArray("de"));QVERIFY(QFile::remove(base+"diverse.saving"));QVERIFY(QFile::remove(base+"diverse.dat"));write(root.path()+"/unrelated","de");QVERIFY(::symlink((root.path()+"/unrelated").toUtf8().constData(),(base+"diverse.dat").toUtf8().constData())==0);QVERIFY(!storage::readAzureusRemoteDiversifications(**versions,0));QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,update));QCOMPARE(read(root.path()+"/unrelated"),QByteArray("de"));
 }
};
QTEST_GUILESS_MAIN(AzureusRemoteDiversificationTests)
#include "azureus_remote_diversification_tests.moc"
