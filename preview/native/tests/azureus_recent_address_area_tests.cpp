// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_recent_address_area.hpp"
#include <QTest>
#include <QFile>
#include <QTemporaryDir>
namespace storage=biglybtc::storage;
class AzureusRecentAddressAreaTests final:public QObject{Q_OBJECT
private slots:
 void addressAndBlockFilesHaveSeparateCapturedRoles_data(){QTest::addColumn<int>("network");QTest::newRow("main-v4")<<0;QTest::newRow("main-v6")<<3;}
 void addressAndBlockFilesHaveSeparateCapturedRoles(){
  QFETCH(int,network);QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,network);QVERIFY(versions);auto address=storage::readAzureusRecentAddressArea(**versions);auto blocks=storage::readAzureusKeyBlockArea(**versions);QVERIFY(address&&blocks&&!address->primary()&&!blocks->primary());const std::string first="d11:most_recent9:127.0.0.1e";auto updated=storage::saveAzureusRecentAddressArea(**versions,*address,first);QVERIFY(updated);QCOMPARE(updated->primary(),std::optional{first});auto storedBlock=storage::saveAzureusKeyBlockArea(**versions,*blocks,"de");QVERIFY(storedBlock);auto again=storage::readAzureusRecentAddressArea(**versions);QVERIFY(again);QCOMPARE(again->primary(),std::optional{first});const std::string second="d11:most_recent9:127.0.0.2e";QVERIFY(storage::saveAzureusRecentAddressArea(**versions,*again,second));QVERIFY(!storage::saveAzureusRecentAddressArea(**versions,*address,"de"));auto untouched=storage::readAzureusKeyBlockArea(**versions);QVERIFY(untouched);QCOMPARE(untouched->primary(),std::optional<std::string>{"de"});
 }
 void pendingAddressPublicationCannotBeOverwrittenOrBlockOtherArea(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);QFile file(root.path()+"/dht/addresses.saving");QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("de"),qint64{2});file.close();auto address=storage::readAzureusRecentAddressArea(**versions);QVERIFY(address&&!address->primary()&&address->pending()==std::optional<std::string>{"de"});QVERIFY(!storage::saveAzureusRecentAddressArea(**versions,*address,"de"));auto blocks=storage::readAzureusKeyBlockArea(**versions);QVERIFY(blocks);QVERIFY(storage::saveAzureusKeyBlockArea(**versions,*blocks,"de"));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("de"));
 }
};
QTEST_GUILESS_MAIN(AzureusRecentAddressAreaTests)
#include "azureus_recent_address_area_tests.moc"
