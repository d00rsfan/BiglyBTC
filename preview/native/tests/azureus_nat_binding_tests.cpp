// SPDX-License-Identifier: GPL-2.0-or-later
#include "bindings.hpp"
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <set>
using namespace biglybtc;namespace nat=services::azureus_nat;namespace nodes=services::azureus_nodes;
namespace {
nodes::Contact contact(unsigned n,bool v6=false){nodes::Contact c;c.protocol=55;c.address.length=v6?16:4;c.address.port=12345;if(v6){c.address.bytes[0]=0x20;c.address.bytes[1]=1;c.address.bytes[2]=0xd;c.address.bytes[3]=0xb8;c.address.bytes[15]=static_cast<std::uint8_t>(n);}else{c.address.bytes[0]=127;c.address.bytes[1]=53;c.address.bytes[2]=1;c.address.bytes[3]=static_cast<std::uint8_t>(n);}return c;}
}
class AzureusNatBindingTests final:public QObject {
 Q_OBJECT
private slots:
 void boundedBindingsKeepOriginalStrictExpiryAndFailureWindows(){
  for(bool v6:{false,true}){nat::Bindings table;for(unsigned i=1;i<=8;++i){auto b=table.bind(contact(i,v6),0);QVERIFY(b&&b->ok&&b->port==12345);}auto full=table.bind(contact(9,v6),1);QVERIFY(full&&!full->ok&&!full->port);auto key=*nat::socketAddress(contact(1,v6).address);auto old=table.candidate(key,1);QVERIFY(old);auto changed=contact(1,v6);changed.protocol=51;QVERIFY(table.bind(changed,1)->ok);QCOMPARE(table.candidate(key,1)->contact.protocol,55);
   for(int i=1;i<=8;++i){QVERIFY(table.outcome(*old,false,i*1000));}QVERIFY(!table.candidate(key,38000));QVERIFY(!table.bind(changed,38000)->ok);QVERIFY(table.candidate(key,38001));QVERIFY(table.bind(changed,38001)->ok);QVERIFY(table.outcome(*old,true,38002));QVERIFY(table.candidate(key,38002));QCOMPARE(table.expire(300000).size(),std::size_t{0});QCOMPARE(table.expire(300001).size(),std::size_t{7});QCOMPARE(table.size(),std::size_t{1});QCOMPARE(table.expire(338001).size(),std::size_t{0});QCOMPARE(table.expire(338002).size(),std::size_t{1});QVERIFY(table.bind(changed,338003)->ok);QVERIFY(!table.outcome(*old,false,338004));auto current=table.candidate(key,338004);QVERIFY(current&&current->serial!=old->serial);table.clear();QVERIFY(table.bind(changed,338005)->ok);QVERIFY(!table.outcome(*current,false,338006));
  }
  nat::Bindings secondary(true);QVERIFY(!secondary.bind(contact(1),0)->ok);QCOMPARE(secondary.size(),std::size_t{0});nat::Bindings invalid;QVERIFY(!invalid.bind(contact(1),-1));auto c=contact(1);c.address.port=0;QVERIFY(!invalid.bind(c,0));
 }
 void actualOriginalBindingMethods(){
  const auto java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA"),classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_NAT_REFERENCE_CLASSPATH");QVERIFY2(!java.isEmpty()&&!classes.isEmpty(),"Prepare pinned original NAT binding oracle; no SKIP");
  QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/home"));QVERIFY(QDir().mkpath(root.path()+"/tmp"));QProcess peer;auto env=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(env);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+root.path()+"/home","-Dazureus.config.path="+root.path()+"/profile","-Djava.io.tmpdir="+root.path()+"/tmp","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.NatWireOracle"});peer.start();QVERIFY(peer.waitForStarted(3000));const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(2000);}});
  QByteArray commands;std::vector<std::string> expected;
  for(bool v6:{false,true}){nat::Bindings table;const auto add=[&](QByteArray op,const nodes::Contact& c,nat::Milliseconds now,int outcome,std::string value){commands+=op+' '+QByteArray(reinterpret_cast<const char*>(c.address.bytes.data()),c.address.length).toHex()+" "+QByteArray::number(c.address.port)+" "+QByteArray::number(c.protocol)+" "+QByteArray::number(now)+" "+QByteArray::number(outcome)+'\n';expected.push_back(std::move(value));};
   add("RESET",contact(1,v6),0,0,"0");for(unsigned i=1;i<=9;++i){const auto c=contact(i,v6);auto b=table.bind(c,0);QVERIFY(b);nat::Message reply;reply.type=nat::Type::BindReply;reply.ok=b->ok;reply.port=b->port;add("BIND",c,0,0,*nat::encode(reply));}
   const auto c=contact(1,v6);const auto key=*nat::socketAddress(c.address);auto lease=table.candidate(key,0);QVERIFY(lease);for(int i=1;i<=8;++i){QVERIFY(table.outcome(*lease,false,i*1000));add("OUTCOME",c,i*1000,0,"1");add("CHECK",c,i*1000,0,table.candidate(key,i*1000)?"1":"0");}
   for(nat::Milliseconds now:{38000,38001}){add("CHECK",c,now,0,table.candidate(key,now)?"1":"0");auto b=table.bind(c,now);QVERIFY(b);nat::Message reply;reply.type=nat::Type::BindReply;reply.ok=b->ok;reply.port=b->port;add("BIND",c,now,0,*nat::encode(reply));}
   QVERIFY(table.outcome(*lease,true,38002));add("OUTCOME",c,38002,1,"1");add("CHECK",c,38002,0,"1");for(nat::Milliseconds now:{300000,300001,338001,338002}){add("EXPIRE",c,now,0,std::to_string(table.expire(now).size()));}
  }
  addSecondary(commands,expected);
  QCOMPARE(peer.write(commands),commands.size());peer.closeWriteChannel();QVERIFY(peer.waitForFinished(15000));const auto errors=peer.readAllStandardError();QVERIFY2(peer.exitCode()==0&&peer.exitStatus()==QProcess::NormalExit,errors.constData());auto lines=peer.readAllStandardOutput().trimmed().split('\n');QCOMPARE(lines.size(),qsizetype(expected.size()));for(std::size_t i=0;i<expected.size();++i){QCOMPARE(lines[qsizetype(i)],QByteArray::fromStdString(expected[i]).toHex());}QCOMPARE(expected.size(),std::size_t{75});
 }
private:
 static void addSecondary(QByteArray& commands,std::vector<std::string>& expected){commands+="RESET 7f350101 12345 55 0 1\nBIND 7f350101 12345 55 0 0\nCHECK 7f350101 12345 55 0 0\n";expected.push_back("0");expected.push_back("d2:oki0e4:typei1ee");expected.push_back("0");}
};
QTEST_GUILESS_MAIN(AzureusNatBindingTests)
#include "azureus_nat_binding_tests.moc"
