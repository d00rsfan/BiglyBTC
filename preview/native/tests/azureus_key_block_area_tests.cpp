// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_key_block_area.hpp"
#include <QTest>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <unistd.h>
namespace storage=biglybtc::storage;
namespace {
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()){throw std::runtime_error("Generated key-block area");}}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated key-block area read");}return f.readAll();}
const std::string map="d7:entriesle6:opaque4:keepe";
}
class AzureusKeyBlockAreaTests final:public QObject{Q_OBJECT
private slots:
 void exactCapturedOwnerPublishesAndReturnsFreshReceipt(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);auto before=storage::readAzureusKeyBlockArea(**versions);QVERIFY(before&&!before->primary()&&!before->pending());auto saved=storage::saveAzureusKeyBlockArea(**versions,*before,map);QVERIFY(saved&&saved->primary()==std::optional{map}&&!saved->pending());QCOMPARE(read(root.path()+"/dht/block.dat"),QByteArray::fromStdString(map));QVERIFY(!QFile::exists(root.path()+"/dht/block.saving"));QVERIFY(storage::saveAzureusKeyBlockArea(**versions,*saved,map));QVERIFY(!storage::saveAzureusKeyBlockArea(**versions,*before,"de"));QCOMPARE(read(root.path()+"/dht/block.dat"),QByteArray::fromStdString(map));
  auto other=storage::AzureusValueVersions::open(*lease,3);QVERIFY(other);auto isolated=storage::readAzureusKeyBlockArea(**other);QVERIFY(isolated&&!isolated->primary());QVERIFY(!storage::saveAzureusKeyBlockArea(**other,*saved,"de"));
 }
 void pendingAndChangedPreimagesRemainUntouched(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";write(base+"block.saving",QByteArray::fromStdString(map));auto pending=storage::readAzureusKeyBlockArea(**versions);QVERIFY(pending&&!pending->primary()&&pending->pending());QVERIFY(!storage::saveAzureusKeyBlockArea(**versions,*pending,"de"));QCOMPARE(read(base+"block.saving"),QByteArray::fromStdString(map));QVERIFY(QFile::remove(base+"block.saving"));write(base+"block.dat",QByteArray::fromStdString(map));auto before=storage::readAzureusKeyBlockArea(**versions);QVERIFY(before);write(base+"replacement",QByteArray::fromStdString(map));QVERIFY(QFile::remove(base+"block.dat"));QVERIFY(QFile::rename(base+"replacement",base+"block.dat"));QVERIFY(!storage::saveAzureusKeyBlockArea(**versions,*before,"de"));QCOMPARE(read(base+"block.dat"),QByteArray::fromStdString(map));QVERIFY(!QFile::exists(base+"block.saving"));
 }
 void unsafeFilesAndDetachedDirectoriesCannotPublish(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);const auto base=root.path()+"/dht/";write(root.path()+"/unrelated","de");QVERIFY(::symlink((root.path()+"/unrelated").toUtf8().constData(),(base+"block.dat").toUtf8().constData())==0);QVERIFY(!storage::readAzureusKeyBlockArea(**versions));QVERIFY(QFile::remove(base+"block.dat"));QVERIFY(::link((root.path()+"/unrelated").toUtf8().constData(),(base+"block.dat").toUtf8().constData())==0);QVERIFY(!storage::readAzureusKeyBlockArea(**versions));QVERIFY(QFile::remove(base+"block.dat"));auto before=storage::readAzureusKeyBlockArea(**versions);QVERIFY(before);QVERIFY(QDir().rename(root.path()+"/dht",root.path()+"/detached"));QVERIFY(QDir().mkdir(root.path()+"/dht"));QVERIFY(!storage::saveAzureusKeyBlockArea(**versions,*before,map));QVERIFY(!QFile::exists(base+"block.dat"));QCOMPARE(read(root.path()+"/unrelated"),QByteArray("de"));
 }
 void malformedReplacementAndDifferentProfileCannotOverwrite(){
  QTemporaryDir a,b;auto first=storage::ProfileLease::acquire(a.path().toStdString()),second=storage::ProfileLease::acquire(b.path().toStdString());QVERIFY(first&&second);auto av=storage::AzureusValueVersions::open(*first,0),bv=storage::AzureusValueVersions::open(*second,0);QVERIFY(av&&bv);auto before=storage::readAzureusKeyBlockArea(**av);QVERIFY(before);QVERIFY(!storage::saveAzureusKeyBlockArea(**av,*before,"bad"));QVERIFY(!storage::saveAzureusKeyBlockArea(**bv,*before,map));QVERIFY(!QFile::exists(a.path()+"/dht/block.dat"));QVERIFY(!QFile::exists(b.path()+"/dht/block.dat"));
 }
};
QTEST_GUILESS_MAIN(AzureusKeyBlockAreaTests)
#include "azureus_key_block_area_tests.moc"
