// SPDX-License-Identifier: GPL-2.0-or-later
#include "bindings.hpp"
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <algorithm>
using namespace biglybtc;namespace nat=services::azureus_nat;namespace nodes=services::azureus_nodes;
namespace {
nodes::Contact external(bool v6){nodes::Contact c;c.protocol=55;c.address.length=v6?16:4;c.address.port=12345;c.address.bytes[0]=v6?0x20:127;c.address.bytes[v6?15:3]=1;c.instance=7;c.flags=3;return c;}
}
class AzureusNatContactTests final:public QObject {
 Q_OBJECT
private slots:
 void sourcePortChangesNeitherExternalIdentityNorExportedContact(){
  for(bool v6:{false,true}){nodes::NodeCache cache;const auto announced=external(v6);auto physical=announced.address;physical.port=23456;auto route=nat::RoutedContact::observed(announced,physical,cache);QVERIFY(route);QCOMPARE(route->external().address,announced.address);QCOMPARE(route->transport(),physical);QCOMPARE(route->external().instance,7);QCOMPARE(route->external().flags,3);const auto expected=cache.lookup({announced.address,{}},announced.protocol);QVERIFY(expected);QCOMPARE(route->external().id,expected->id);
   auto exported=nodes::encodeContact(route->exportSnapshot());QVERIFY(exported);auto decoded=nodes::decodeContact(*exported,cache);QVERIFY(decoded);QCOMPARE(decoded->contact.address,announced.address);physical.port=34567;auto updated=route->withTransport(physical);QVERIFY(updated);QCOMPARE(updated->transport(),physical);QCOMPARE(updated->external().id,route->external().id);QCOMPARE(updated->external().address,announced.address);auto reexported=nodes::encodeContact(updated->exportSnapshot());QVERIFY(reexported);QCOMPARE(*reexported,*exported);physical.bytes[v6?15:3]=2;QVERIFY(!route->withTransport(physical));QVERIFY(!nat::RoutedContact::observed(announced,physical,cache));}
 }
 void bindReportsExternalPortAndRetainsInitialPhysicalRouteOnRebind(){
  for(bool v6:{false,true}){nodes::NodeCache cache;auto e=external(v6);auto actual=e.address;actual.port=23456;auto first=nat::RoutedContact::observed(e,actual,cache);QVERIFY(first);nat::Bindings table;auto reply=table.bind(*first,0);QVERIFY(reply&&reply->ok);QCOMPARE(reply->port,std::optional<std::uint16_t>{12345});auto key=*nat::socketAddress(e.address);auto lease=table.candidate(key,0);QVERIFY(lease);QCOMPARE(lease->contact.address.port,12345);QCOMPARE(lease->transport.port,23456);e.protocol=51;actual.port=34567;auto second=nat::RoutedContact::observed(e,actual,cache);QVERIFY(second);QVERIFY(table.bind(*second,1)->ok);auto retained=table.candidate(key,1);QVERIFY(retained);QCOMPARE(retained->serial,lease->serial);QCOMPARE(retained->contact.protocol,55);QCOMPARE(retained->transport.port,23456);QVERIFY(table.outcome(*lease,true,2));}
 }
 void mappedAddressesCanonicaliseAndInvalidEndpointsHaveNoRoute(){
  nodes::NodeCache cache;auto c=external(false);services::azureus_dht::Address mapped;mapped.length=16;mapped.port=23456;mapped.bytes[10]=255;mapped.bytes[11]=255;std::copy_n(c.address.bytes.begin(),4,mapped.bytes.begin()+12);auto route=nat::RoutedContact::observed(c,mapped,cache);QVERIFY(route);QCOMPARE(route->transport().length,4);mapped.port=0;QVERIFY(!nat::RoutedContact::observed(c,mapped,cache));auto bad=c.address;bad.length=8;QVERIFY(!nat::RoutedContact::observed(c,bad,cache));
 }
 void actualOriginalContactConstructorAndAddressRoles(){
  const auto java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA"),classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_NAT_REFERENCE_CLASSPATH");QVERIFY2(!java.isEmpty()&&!classes.isEmpty(),"Prepare pinned original contact-role oracle; no SKIP");QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/home"));QVERIFY(QDir().mkpath(root.path()+"/tmp"));QProcess peer;auto env=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(env);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+root.path()+"/home","-Dazureus.config.path="+root.path()+"/profile","-Djava.io.tmpdir="+root.path()+"/tmp","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.NatWireOracle"});peer.start();QVERIFY(peer.waitForStarted(3000));const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(2000);}});QByteArray commands;std::vector<std::string> expected;nodes::NodeCache cache;
  const auto hexAddress=[](const auto& a){return QByteArray(reinterpret_cast<const char*>(a.bytes.data()),a.length).toHex();};
  for(bool v6:{false,true}){for(int version:{32,33,50,55}){auto e=external(v6);e.protocol=static_cast<std::int8_t>(version);for(int port:{12345,23456,34567}){for(bool update:{false,true}){auto actual=e.address;actual.port=static_cast<std::uint16_t>(port);auto route=nat::RoutedContact::observed(e,actual,cache);QVERIFY(route);if(update){route=route->withTransport([&]{auto changed=actual;changed.port=45678;return changed;}());QVERIFY(route);}commands+=(update?"ROUTESET ":"ROUTE ")+hexAddress(e.address)+" 12345 "+QByteArray::number(version)+' '+hexAddress(actual)+' '+QByteArray::number(port)+(update?" 45678":"")+'\n';expected.push_back(*nat::socketAddress(route->external().address)+"|"+*nat::socketAddress(route->transport())+"|"+QByteArray(reinterpret_cast<const char*>(route->external().id.data()),20).toHex().toStdString());}}
   auto invalid=e.address;invalid.bytes[v6?15:3]=2;QVERIFY(!nat::RoutedContact::observed(e,invalid,cache));commands+="ROUTE "+hexAddress(e.address)+" 12345 "+QByteArray::number(version)+' '+hexAddress(invalid)+" 12345\n";expected.push_back("INVALID");}}
  QCOMPARE(peer.write(commands),commands.size());peer.closeWriteChannel();QVERIFY(peer.waitForFinished(15000));const auto errors=peer.readAllStandardError();QVERIFY2(peer.exitCode()==0&&peer.exitStatus()==QProcess::NormalExit,errors.constData());auto lines=peer.readAllStandardOutput().trimmed().split('\n');QCOMPARE(lines.size(),qsizetype(expected.size()));for(std::size_t i=0;i<expected.size();++i){QCOMPARE(lines[qsizetype(i)],QByteArray::fromStdString(expected[i]).toHex());}QCOMPARE(expected.size(),std::size_t{56});
 }

};
QTEST_GUILESS_MAIN(AzureusNatContactTests)
#include "azureus_nat_contact_tests.moc"
