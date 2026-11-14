// SPDX-License-Identifier: GPL-2.0-or-later
#include "wire.hpp"
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <QCryptographicHash>
using namespace biglybtc;namespace nat=services::azureus_nat;namespace nodes=services::azureus_nodes;
namespace {
services::azureus_dht::Address address(bool v6){services::azureus_dht::Address a;a.length=v6?16:4;a.port=12345;if(v6){a.bytes[0]=0x20;a.bytes[1]=1;a.bytes[2]=0xd;a.bytes[3]=0xb8;a.bytes[15]=1;}else{a.bytes[0]=127;a.bytes[1]=52;a.bytes[2]=3;a.bytes[3]=1;}return a;}
std::string bytes(const auto& a){return {reinterpret_cast<const char*>(a.data()),a.size()};}
}
class AzureusNatWireTests final:public QObject {
 Q_OBJECT
private slots:
 void contactsAndPublicationUseOriginalExportNotAContactMap(){
  for(bool v6:{false,true}){const auto a=address(v6);auto encoded=nat::encodePublishValue({true,55,a});QVERIFY(encoded);QCOMPARE(encoded->size(),v6?std::size_t{22}:std::size_t{10});QCOMPARE(static_cast<unsigned char>((*encoded)[0]),0);QCOMPARE(static_cast<unsigned char>((*encoded)[1]),1);QCOMPARE(static_cast<unsigned char>((*encoded)[2]),55);nodes::NodeCache cache;auto decoded=nat::decodePublishValue(*encoded,cache);QVERIFY(decoded);QCOMPARE(decoded->address,a);QCOMPARE(decoded->protocol,55);QVERIFY(!nat::decodePublishValue(*encoded+'x',cache));auto bad=*encoded;bad[0]=1;QVERIFY(!nat::decodePublishValue(bad,cache));bad=*encoded;bad[1]=2;QVERIFY(!nat::decodePublishValue(bad,cache));for(std::size_t i=0;i<encoded->size();++i){QVERIFY(!nat::decodePublishValue(std::string_view(*encoded).substr(0,i),cache));}}
  auto a=address(false);a.port=0;QVERIFY(!nat::encodePublishValue({true,55,a}));QVERIFY(!nat::publishKey(std::array<std::uint8_t,19>{}));QCOMPARE(nat::handlerKey(),QCryptographicHash::hash("Aelitis:NATPuncher:TransferHandlerKey",QCryptographicHash::Sha1).toStdString());
  QCOMPARE(*nat::socketAddress(address(false)),std::string("/127.52.3.1:12345"));QCOMPARE(*nat::socketAddress(address(true)),std::string("/[2001:db8:0:0:0:0:0:1]:12345"));
 }
 void typedMessagesRetainOnlyBoundedDataAndNeverExecuteDebug(){
  nodes::NodeCache cache;auto origin=nodes::encodeContact({true,55,address(false)});QVERIFY(origin);
  for(unsigned i=0;i<12;++i){nat::Message m;m.type=static_cast<nat::Type>(i);if(i==1||i==3||i==5||i==9||i==11){m.ok=true;}if(i==2){m.target=*nat::socketAddress(address(false));}if(i==4){m.origin=*origin;m.port=12346;}if(i==9){m.ip="127.52.3.1";m.port=12346;}if(i==2||i==3||i==4||i==5){m.clientData="d4:kind1:xe";}auto encoded=nat::encode(m);QVERIFY(encoded);auto decoded=nat::decode(*encoded,cache);QVERIFY(decoded);QCOMPARE(*decoded,m);}
  for(const auto* invalid:{"de","d4:typei12ee","d4:typei-1ee","d4:typei0e4:typei0ee","d4:typei1ee","d2:oki2e4:typei1ee","d4:typei2ee","d4:typei4ee","d6:_debugde4:typei0ee","d11:client_data4:nope4:typei0ee","d11:client_datad1:ai1e1:ai2ee4:typei0ee","d4:porti65536e4:typei0ee"}){QVERIFY(!nat::decode(invalid,cache));}
  nat::Message huge;huge.clientData="d1:x"+std::to_string(nat::MaximumMessage)+":"+std::string(nat::MaximumMessage,'x')+"e";QVERIFY(!nat::encode(huge));QVERIFY(!nat::decode(std::string(nat::MaximumMessage+1,'x'),cache));
  nat::Message bad;bad.clientData="d1:ai1e1:ai2ee";QVERIFY(!nat::encode(bad));bad={};bad.type=nat::Type::Connect;bad.origin=*origin+'x';QVERIFY(!nat::encode(bad));
 }
 void actualOriginalWireBuilders(){
  const auto java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA"),classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_NAT_REFERENCE_CLASSPATH");QVERIFY2(!java.isEmpty()&&!classes.isEmpty(),"Prepare pinned original NAT wire oracle; no SKIP or model substitution");
  QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/home"));QVERIFY(QDir().mkpath(root.path()+"/tmp"));QProcess peer;auto env=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(env);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+root.path()+"/home","-Dazureus.config.path="+root.path()+"/profile","-Djava.io.tmpdir="+root.path()+"/tmp","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.NatWireOracle"});peer.start();QVERIFY(peer.waitForStarted(3000));const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(2000);}});
  QByteArray commands;std::vector<std::string> expected;nodes::NodeCache cache;
  for(bool v6:{false,true}){for(int version:{51,55}){const auto a=address(v6);const auto args=QByteArray(reinterpret_cast<const char*>(a.bytes.data()),a.length).toHex()+" "+QByteArray::number(a.port)+" "+QByteArray::number(version);nodes::Snapshot snapshot{true,static_cast<std::int8_t>(version),a};auto contact=nodes::encodeContact(snapshot);QVERIFY(contact);
   const auto add=[&](QByteArray op,std::string value){commands+=op+' '+args+'\n';expected.push_back(std::move(value));};add("CONTACT",*contact);add("PUBLISH",*nat::encodePublishValue(snapshot));add("SOCKET",*nat::socketAddress(a));std::array<std::uint8_t,20> id;for(unsigned i=0;i<20;++i){id[i]=i;}add("KEY",*nat::publishKey(id));
   for(int type:{0,2,4,6,7,8,10}){for(int mode:{0,1,2}){nat::Message m;m.type=static_cast<nat::Type>(type);if(type==2){m.target=*nat::socketAddress(a);}if(type==4){m.origin=*contact;m.port=a.port+1;}if((type==2||type==4)&&mode){m.clientData=type==2&&mode==2?"d9:_notunneli1e4:kind1:xe":"d4:kind1:xe";}auto encoded=nat::encode(m);QVERIFY(encoded);commands+="MAP "+args+' '+QByteArray::number(type)+' '+QByteArray::number(mode)+'\n';expected.push_back(*encoded);}}
  }}
  QCOMPARE(peer.write(commands),commands.size());peer.closeWriteChannel();QVERIFY2(peer.waitForFinished(15000),peer.readAllStandardError().constData());QVERIFY2(peer.exitCode()==0&&peer.exitStatus()==QProcess::NormalExit,peer.readAllStandardError().constData());const auto output=peer.readAllStandardOutput().trimmed().split('\n');QCOMPARE(output.size(),qsizetype(expected.size()));for(std::size_t i=0;i<expected.size();++i){QCOMPARE(output[qsizetype(i)],QByteArray::fromStdString(expected[i]).toHex());}QCOMPARE(expected.size(),std::size_t{100});
 }
};
QTEST_GUILESS_MAIN(AzureusNatWireTests)
#include "azureus_nat_wire_tests.moc"
