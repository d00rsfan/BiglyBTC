// SPDX-License-Identifier: GPL-2.0-or-later
#include "recent_addresses.hpp"
#include <QTest>
#include <QFile>
namespace identity=biglybtc::services::azureus_identity;namespace core=biglybtc::core;
std::string unhex(const QByteArray& raw){return QByteArray::fromHex(raw).toStdString();}
class AzureusRecentAddressTests final:public QObject{Q_OBJECT
private slots:
 void actualOriginalCachesAndHostText(){
  QFile file(QString::fromUtf8(RECENT_ADDRESS_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned cases=0,formats=0;
  while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');if(f[0]=="format"){QHostAddress address(QString::fromStdString(unhex(f[1])));auto formatted=identity::originalHostAddress(address);QVERIFY(formatted);QCOMPARE(*formatted,unhex(f[2]));++formats;continue;}QCOMPARE(f.size(),9);auto input=core::Document::parse(unhex(f[3]));QVERIFY(input);const auto address=unhex(f[4]);std::expected<std::string,identity::Error> output=f[1]=="load"?std::expected<std::string,identity::Error>{input->serialize()}:identity::remember(input->root(),address,f[2].toLongLong());QVERIFY(output);QCOMPARE(*output,unhex(f[8]));auto after=core::Document::parse(*output);QVERIFY(after);auto recent=identity::mostRecent(after->root());QVERIFY(recent);QCOMPARE(recent->has_value(),f[5]!="-");if(*recent){QCOMPARE(**recent,unhex(f[5]));}auto old=identity::isRecent(after->root(),"127.1.2.1"),current=identity::isRecent(after->root(),address);QVERIFY(old&&current);QCOMPARE(*old,f[6]=="1");QCOMPARE(*current,f[7]=="1");++cases;
  }QCOMPARE(cases,24u);QCOMPARE(formats,6u);
 }
 void malformedTimestampIsNotSilentlyDroppedAndReservedSlotCanRepair(){
  auto input=core::Document::parse("d6:broken4:keepe");QVERIFY(input);QVERIFY(!identity::remember(input->root(),"127.0.0.1",1000));QVERIFY(identity::isRecent(input->root(),"broken").value());auto bad=core::Document::parse("d11:most_recenti1ee");QVERIFY(bad);QVERIFY(!identity::mostRecent(bad->root()));QVERIFY(identity::isRecent(bad->root(),"most_recent").value());auto repaired=identity::remember(bad->root(),"127.0.0.1",1000);QVERIFY(repaired);auto after=core::Document::parse(*repaired);QVERIFY(after);QCOMPARE(identity::mostRecent(after->root()).value(),std::optional<std::string>{"127.0.0.1"});QVERIFY(!identity::originalHostAddress(QHostAddress{}));QVERIFY(!identity::remember(input->root(),std::string(4097,'x'),1000));
 }
};
QTEST_GUILESS_MAIN(AzureusRecentAddressTests)
#include "azureus_recent_address_tests.moc"
