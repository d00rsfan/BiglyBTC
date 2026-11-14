// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_remote_diversification.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <unistd.h>
using namespace biglybtc;
namespace {
void write(const QString& path,const QByteArray& data){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()){throw std::runtime_error("Generated diversification fixture");}}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Read generated fixture");}return file.readAll();}
}
class AzureusRemoteDiversificationTests final:public QObject {
 Q_OBJECT
private slots:
 void originalRemoteOffsetsSurviveMergeAndReopen(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,3);QVERIFY(versions);const auto base=root.path()+"/dht/net3/";
  const QByteArray fixture="d5:locall4:keepe6:remoteld3:expi900000e3:fpoli2ei7ee3:key20:aaaaaaaaaaaaaaaaaaaa4:typei3eee7:unknown4:keepe";write(base+"diverse.dat",fixture);
  auto loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded);QCOMPARE(loaded->size(),std::size_t{1});QCOMPARE((*loaded)[0].offsets,(std::array<unsigned,2>{2,7}));
  core::AzureusRemoteDiversification added;added.key.fill('b');added.type=2;added.expiry=800000;added.offsets={1,9};const std::array updates{added};QVERIFY(storage::saveAzureusRemoteDiversifications(**versions,updates));auto merged=core::Document::parse(read(base+"diverse.dat").toStdString());QVERIFY(merged);QCOMPARE(merged->find("unknown")->bytes,std::string_view("keep"));QCOMPARE(merged->find("local")->children[0].bytes,std::string_view("keep"));QCOMPARE(merged->find("remote")->children.size(),std::size_t{2});QVERIFY(!QFile::exists(base+"diverse.saving"));
  loaded=storage::readAzureusRemoteDiversifications(**versions,1000);QVERIFY(loaded&&loaded->size()==2);auto reopened=storage::AzureusValueVersions::open(*lease,3);QVERIFY(reopened);auto again=storage::readAzureusRemoteDiversifications(**reopened,1000);QVERIFY(again);QCOMPARE(*again,*loaded);
  loaded=storage::readAzureusRemoteDiversifications(**versions,850000);QVERIFY(loaded&&loaded->size()==1);QCOMPARE((*loaded)[0].offsets,(std::array<unsigned,2>{2,7}));
 }
 void pendingAndUnsafeSourcesDoNotOverwriteOriginalState(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";write(base+"diverse.dat","d6:remotelee");write(base+"diverse.saving","de");core::AzureusRemoteDiversification r;r.key.fill('c');r.type=3;r.expiry=900000;r.offsets={3,8};const std::array update{r};const auto before=read(base+"diverse.dat");QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,update));QCOMPARE(read(base+"diverse.dat"),before);QCOMPARE(read(base+"diverse.saving"),QByteArray("de"));QVERIFY(QFile::remove(base+"diverse.saving"));QVERIFY(QFile::remove(base+"diverse.dat"));write(root.path()+"/unrelated","de");QVERIFY(::symlink((root.path()+"/unrelated").toUtf8().constData(),(base+"diverse.dat").toUtf8().constData())==0);QVERIFY(!storage::readAzureusRemoteDiversifications(**versions,0));QVERIFY(!storage::saveAzureusRemoteDiversifications(**versions,update));QCOMPARE(read(root.path()+"/unrelated"),QByteArray("de"));
 }
};
QTEST_GUILESS_MAIN(AzureusRemoteDiversificationTests)
#include "azureus_remote_diversification_tests.moc"
