// SPDX-License-Identifier: GPL-2.0-or-later
#include "address_reports.hpp"
#include <QTest>
#include <QFile>
namespace identity=biglybtc::services::azureus_identity;
class AzureusAddressReportPolicyTests final:public QObject{Q_OBJECT
private slots:
 void actualOriginalGatesAndWallClockBoundaries(){QFile file(QString::fromUtf8(ADDRESS_REPORT_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned count=0;while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');QCOMPARE(f.size(),11);const bool v6=f[0]=="6";identity::ReportState state{f[3].toLongLong(),f[4].toInt(),f[7]=="1"};identity::ReportInput input{v6,QHostAddress(v6?"2001:db8::1":"198.51.100.1"),QHostAddress(QString::fromUtf8(f[1])),QHostAddress(v6?"2001:db8::2":"198.51.100.2"),f[2].toLongLong(),std::size_t(f[5].toUInt()),f[6]=="1"};auto result=identity::addressReport(state,input);QVERIFY(result);QCOMPARE(result->state.lastChange,f[8].toLongLong());QCOMPARE(result->state.minimumPeriod,f[9].toInt());QCOMPARE(result->state.deferred,f[10]=="1");if(result->action==identity::ReportAction::Recheck){QCOMPARE(result->state.lastChange,input.wallMilliseconds);}++count;}QCOMPARE(count,230u);}
 void unresolvedCannotStartARecheck(){identity::ReportInput input;input.current=QHostAddress("198.51.100.1");input.reporter=QHostAddress("198.51.100.2");input.wallMilliseconds=10000000;input.observedContacts=32;auto result=identity::addressReport({},input);QVERIFY(!result);QCOMPARE(result.error(),identity::ReportError::Unresolved);}
};
QTEST_GUILESS_MAIN(AzureusAddressReportPolicyTests)
#include "azureus_address_report_policy_tests.moc"
