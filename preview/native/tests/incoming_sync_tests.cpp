// SPDX-License-Identifier: GPL-2.0-only
#include "../services/msgsync/incoming_sync.hpp"
#include <QProcess>
#include <QTest>
using biglybtc::services::msgsync::IncomingSync;
class IncomingSyncTests final:public QObject{
 Q_OBJECT
private slots:
 void statusMatchesTheOriginalMethod(){
  const auto java=qEnvironmentVariable("BIGLYBTC_INCOMING_SYNC_JAVA");if(java.isEmpty()){QSKIP("Original Java status method not configured");}QProcess process;process.start(java,{"-cp",qEnvironmentVariable("BIGLYBTC_INCOMING_SYNC_CLASSPATH"),"IncomingSyncOracle"});QVERIFY(process.waitForStarted());auto ask=[&](QByteArray line){process.write(line+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}return process.readLine().trimmed();};unsigned observations=0;
  for(unsigned scenario=0;scenario<64;++scenario){IncomingSync state;QCOMPARE(ask("Z"),QByteArray("OK"));std::int64_t now=0;auto query=[&](std::int64_t at){++observations;QCOMPARE(ask("S "+QByteArray::number(qlonglong(at))),QByteArray::number(state.state(at)));};query(now);state.attempt(1);QCOMPARE(ask("A 1"),QByteArray("OK"));query(1);query(9999);query(10000);
   for(unsigned step=0;step<20;++step){now+=step%3==0?120000:step%3==1?9999:10000;const std::int32_t more=(scenario>>(step%6))&1?int(step%7+1):0;state.reply(more,now);QCOMPARE(ask("R "+QByteArray::number(qlonglong(now))+' '+QByteArray::number(more)),QByteArray("OK"));query(now);query(now+9999);now+=10000;query(now);}
   now+=119999;query(now);query(now+1);query(now+10001);
  }
  QCOMPARE(observations,4288U);process.closeWriteChannel();QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
 }
 void noAttemptAndThreeQuietRepliesRetainOriginalStates(){
  IncomingSync state;QCOMPARE(state.state(0),-1);state.attempt(1);state.reply(0,2);QCOMPARE(state.state(10000),-1);QCOMPARE(state.state(120001),-1);QCOMPARE(state.state(130001),0);state.reply(5,130002);QCOMPARE(state.state(140001),5);state.reply(0,140002);state.reply(0,140003);state.reply(0,140004);QCOMPARE(state.state(150001),0);
 }
};
QTEST_GUILESS_MAIN(IncomingSyncTests)
#include "incoming_sync_tests.moc"
