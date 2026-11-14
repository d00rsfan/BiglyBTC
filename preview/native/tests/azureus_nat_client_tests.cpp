// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.hpp"
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <limits>
using namespace biglybtc;namespace nat=services::azureus_nat;
namespace {services::azureus_nodes::Contact contact(bool v6=false){services::azureus_nodes::Contact c;c.protocol=55;c.address.length=v6?16:4;c.address.port=12345;c.address.bytes[0]=v6?0x20:127;c.address.bytes[v6?15:3]=1;return c;}}
class AzureusNatClientTests final:public QObject {
 Q_OBJECT
private slots:
 void initialTwoFailuresAndOperationalFourFailuresMatchOriginal(){
  nat::ClientBinding client;QVERIFY(client.select(contact(),0));QCOMPARE(client.failures(),2U);auto first=client.begin(0);QVERIFY(first);QCOMPARE(first->end,30000);QVERIFY(!client.begin(1));auto failed=client.complete(*first,nat::BindStatus::Failed,30000);QVERIFY(failed.accepted&&!failed.failedTarget);QCOMPARE(client.failures(),3U);QVERIFY(!client.begin(49999));auto second=client.begin(50000);QVERIFY(second);failed=client.complete(*second,nat::BindStatus::Failed,80000);QVERIFY(failed.accepted&&failed.failedTarget);QVERIFY(!client.target());
  QVERIFY(client.select(contact(),90000));first=client.begin(90000);QVERIFY(first);QVERIFY(client.complete(*first,nat::BindStatus::Ok,90001).accepted);QVERIFY(client.active());QCOMPARE(client.failures(),0U);
  for(int i=1;i<=4;++i){auto attempt=client.begin(90000+i*50000);QVERIFY(attempt);auto result=client.complete(*attempt,nat::BindStatus::Failed,attempt->end);QVERIFY(result.accepted);QCOMPARE(result.failedTarget,i==4);QCOMPARE(client.active(),i!=4);QCOMPARE(client.failures(),unsigned(i));}
 }
 void refusalDeadlineSourceResetAndStaleCompletionsCannotRestoreAClient(){
  nat::ClientBinding c;QVERIFY(c.select(contact(),0));auto old=c.begin(0);QVERIFY(old);QVERIFY(!c.complete(*old,nat::BindStatus::Ok,30000).accepted);QVERIFY(c.complete(*old,nat::BindStatus::Failed,30000).accepted);QVERIFY(c.select(contact(true),40000));auto next=c.begin(40000);QVERIFY(next);QVERIFY(!c.complete(*old,nat::BindStatus::Ok,40001).accepted);QVERIFY(c.complete(*next,nat::BindStatus::Denied,40001).failedTarget);QVERIFY(!c.target());QVERIFY(c.select(contact(),50000));next=c.begin(50000);c.clear();QVERIFY(!c.complete(*next,nat::BindStatus::Ok,50001).accepted);QVERIFY(!c.active());QVERIFY(!c.select(contact(),-1));QVERIFY(!c.select(contact(),std::numeric_limits<nat::Milliseconds>::max()));
 }
 void tunnelBurstHasFourOpportunitiesAndStopsOnAckWithoutAnEarlyWaitResult(){
  auto burst=nat::TunnelBurst::start(0);QVERIFY(burst);for(int i=0;i<4;++i){auto attempt=burst->begin(i*3000);QVERIFY(attempt);QCOMPARE(attempt->number,unsigned(i+1));QCOMPARE(attempt->end,(i+1)*3000);QVERIFY(!burst->begin(i*3000));}QVERIFY(!burst->begin(12000));QVERIFY(burst->done(12000));auto delayed=nat::TunnelBurst::start(0);QVERIFY(delayed->begin(0));QVERIFY(delayed->begin(8000));QVERIFY(!delayed->begin(8000));delayed->acknowledged();QVERIFY(!delayed->begin(11000));
  auto p=nat::Punch::accepted(contact(),23456,0);QVERIFY(p);QCOMPARE(p->target().address.port,23456);QVERIFY(p->outbound(0));p->outboundAcknowledged();QVERIFY(!p->outbound(3000));QVERIFY(!p->finish(9999));auto result=p->finish(10000);QVERIFY(result&&!result->directObserved);QCOMPARE(result->target.address.port,23456);
 }
 void actualInboundAddressWinsOverRendezvousPortOnlyForTheExpectedIp(){
  for(bool v6:{false,true}){auto c=contact(v6);auto punch=nat::Punch::accepted(c,23456,0);QVERIFY(punch);auto foreign=c.address;foreign.bytes[v6?15:3]=2;foreign.port=34567;QVERIFY(!punch->inbound(foreign,1));auto actual=c.address;actual.port=34567;QVERIFY(punch->inbound(actual,2));QVERIFY(!punch->inbound(c.address,3));auto result=punch->finish(2);QVERIFY(result&&result->directObserved);QCOMPARE(result->target.address.port,34567);QVERIFY(!punch->outbound(3000));punch->close();QVERIFY(!punch->finish(4));
   auto late=nat::Punch::accepted(c,0,0);QVERIFY(late);QVERIFY(!late->inbound(actual,10000));QCOMPARE(late->finish(10000)->target.address.port,c.address.port);}
 }
 void actualOriginalClientBindResultCounters(){
  const auto java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA"),classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_NAT_REFERENCE_CLASSPATH");QVERIFY2(!java.isEmpty()&&!classes.isEmpty(),"Prepare pinned original bind-result oracle; no SKIP");QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/home"));QVERIFY(QDir().mkpath(root.path()+"/tmp"));QProcess peer;auto env=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(env);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+root.path()+"/home","-Dazureus.config.path="+root.path()+"/profile","-Djava.io.tmpdir="+root.path()+"/tmp","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.NatWireOracle"});peer.start();QVERIFY(peer.waitForStarted(3000));const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(2000);}});QByteArray commands;std::vector<std::string> expected;
  for(bool v6:{false,true}){nat::ClientBinding client;const auto c=contact(v6);const auto args=QByteArray(reinterpret_cast<const char*>(c.address.bytes.data()),c.address.length).toHex()+" 12345 55 ";unsigned notifications=0;
   const auto select=[&](nat::Milliseconds now){QVERIFY(client.select(c,now));notifications=0;commands+="CSELECT "+args+QByteArray::number(now)+" 0\n";expected.push_back("2,0");};
   const auto result=[&](nat::Milliseconds now,nat::BindStatus status,int original){auto attempt=client.begin(now);QVERIFY(attempt);auto done=client.complete(*attempt,status,now+(status==nat::BindStatus::Failed?30000:1));QVERIFY(done.accepted);if(done.failedTarget){++notifications;}commands+="CRESULT "+args+QByteArray::number(now)+' '+QByteArray::number(original)+'\n';expected.push_back(std::to_string(client.failures())+","+std::to_string(notifications));};
   select(0);result(0,nat::BindStatus::Failed,2);result(50000,nat::BindStatus::Failed,2);select(100000);result(100000,nat::BindStatus::Ok,0);for(int i=1;i<=4;++i){result(100000+i*50000,nat::BindStatus::Failed,2);}select(400000);result(400000,nat::BindStatus::Denied,1);select(500000);result(500000,nat::BindStatus::Ok,0);result(550000,nat::BindStatus::Failed,2);result(600000,nat::BindStatus::Denied,1);
  }
  QCOMPARE(peer.write(commands),commands.size());peer.closeWriteChannel();QVERIFY(peer.waitForFinished(15000));const auto errors=peer.readAllStandardError();QVERIFY2(peer.exitCode()==0&&peer.exitStatus()==QProcess::NormalExit,errors.constData());const auto lines=peer.readAllStandardOutput().trimmed().split('\n');QCOMPARE(lines.size(),qsizetype(expected.size()));for(std::size_t i=0;i<expected.size();++i){QCOMPARE(lines[qsizetype(i)],QByteArray::fromStdString(expected[i]).toHex());}QCOMPARE(expected.size(),std::size_t{30});
 }

};
QTEST_GUILESS_MAIN(AzureusNatClientTests)
#include "azureus_nat_client_tests.moc"
