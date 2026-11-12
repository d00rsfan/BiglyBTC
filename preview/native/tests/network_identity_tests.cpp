// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_identity_controller.hpp"
#include "network_identity_view.hpp"
#include <QTest>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
using namespace biglybtc;namespace ni=services::network_identity;
namespace {
class Dns final:public QObject {
public:
 QUdpSocket socket;QStringList queries;bool drop=false,delay=false;
 Dns(){if(!socket.bind(QHostAddress::LocalHost,0)){throw std::runtime_error("Owned DNS bind");}connect(&socket,&QUdpSocket::readyRead,this,[this]{while(socket.hasPendingDatagrams()){auto datagram=socket.receiveDatagram();auto data=datagram.data();qsizetype pos=12;QString query;while(pos<data.size()){int len=quint8(data[pos++]);if(!len){break;}if(len>63||pos+len>data.size()){return;}if(!query.isEmpty()){query+='.';}query+=QString::fromLatin1(data.mid(pos,len));pos+=len;}if(pos+4>data.size()){continue;}query=query.toLower();queries<<query;if(drop){continue;}QByteArray value;if(query.endsWith("origin.asn.cymru.com")){value=query.startsWith("4.4.")?"64501 | 8.8.4.0/24 | ZZ | test | 2000-01-01":"64500 | 8.8.8.0/24 | ZZ | test | 2000-01-01";}else if(query.endsWith("origin6.asn.cymru.com")){value="64502 | 2606:4700::/32 | ZZ | test | 2000-01-01";}else if(query=="as64500.asn.cymru.com"){value="64500 | ZZ | test | 2000-01-01 | Generated Provider A";}else if(query=="as64501.asn.cymru.com"){value="64501 | ZZ | test | 2000-01-01 | Generated Provider B";}else if(query=="as64502.asn.cymru.com"){value="64502 | ZZ | test | 2000-01-01 | Generated Provider V6";}else{throw std::runtime_error("Unexpected owned DNS query: "+query.toStdString());}QByteArray out=data.first(2)+QByteArray::fromHex("81800001000100000000")+data.mid(12,pos+4-12)+QByteArray::fromHex("c00c001000010000003c");const auto len=value.size()+1;out+=char(len>>8);out+=char(len);out+=char(value.size());out+=value;const auto send=[this,out,address=datagram.senderAddress(),port=datagram.senderPort()]{socket.writeDatagram(out,address,port);};if(delay){QTimer::singleShot(80,this,send);}else{send();}}});}
};
ui::RuntimeMetricsPresentation input(quint64 sequence,QString ipv4="8.8.8.8",QString ipv6={},bool dns=true,quint64 session=1){auto packet=std::make_shared<ui::RuntimeMetricsPacket>();packet->session=session;packet->epoch=1;packet->sequence=sequence;auto info=std::make_shared<ni::Input>();info->addresses={ipv4,ipv6};info->dnsAllowed=dns;packet->networkInput=std::move(info);return packet;}
ui::RuntimeMetricsPresentation last(const QSignalSpy& spy){return qvariant_cast<ui::RuntimeMetricsPresentation>(spy.last().first());}
}
class NetworkIdentityTests final:public QObject {
 Q_OBJECT
private slots:
 void codecsAndOriginalSavedCache(){
  QCOMPARE(ni::originQuery(QHostAddress("8.8.4.4")),QString("4.4.8.8.origin.asn.cymru.com"));QCOMPARE(ni::originQuery(QHostAddress("2606:4700::1111")),QString("1.1.1.1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.7.4.6.0.6.2.origin6.asn.cymru.com"));
  QVERIFY(ni::publicAddress(QHostAddress("8.8.8.8")));QVERIFY(!ni::publicAddress(QHostAddress("127.0.0.1")));QVERIFY(!ni::publicAddress(QHostAddress("192.168.1.1")));QVERIFY(!ni::publicAddress(QHostAddress("fd00::1")));
  QVERIFY(!ni::parseOrigin(QHostAddress("8.8.8.8"),"64500 | 8.8.4.0/24 | ZZ"));QVERIFY(!ni::parseOrigin(QHostAddress("8.8.8.8"),"AS64500.bad | 8.8.8.0/24"));QVERIFY(!ni::parseName("64500","64501 | ZZ | test | date | Wrong provider"));
  auto document=core::Document::parse("de");QVERIFY(document);QVERIFY(document->replaceValue("ASN Details","ld2:as5:645004:name18:Generated Provider3:bgp10:8.8.8.0/24ee"));auto saved=ni::saved(&*document);QCOMPARE(saved.size(),std::size_t(1));QVERIFY(ni::matches(saved[0],QHostAddress("8.8.8.8")));QVERIFY(!ni::matches(saved[0],QHostAddress("8.8.4.4")));
 }
 void actualAsyncDnsBothFamiliesAndDisplay(){
  Dns dns;app::NetworkIdentityController owner(nullptr,QHostAddress::LocalHost,dns.socket.localPort(),2000);owner.beginSession(1);QSignalSpy spy(&owner,&app::NetworkIdentityController::ready);owner.observe(input(1,"8.8.8.8","2606:4700::1111"));QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty()&&last(spy)->network->families[0].asn&&last(spy)->network->families[0].asn->name=="Generated Provider A"&&last(spy)->network->families[1].asn&&last(spy)->network->families[1].asn->name=="Generated Provider V6",5000);QCOMPARE(dns.queries.size(),4);
  ui::NetworkIdentityView view;view.setSnapshot(last(spy)->network);QCOMPARE(view.findChild<QLabel*>("network-ipv4-address")->text(),QString("8.8.8.8"));QCOMPARE(view.findChild<QLabel*>("network-ipv6-asn")->text(),QString("AS64502"));QCOMPARE(view.findChild<QLabel*>("network-ipv4-provider")->text(),QString("Generated Provider A"));owner.observe(input(2,"8.8.8.8","2606:4700::1111"));QTest::qWait(40);QCOMPARE(dns.queries.size(),4);view.setSnapshot({});QCOMPARE(view.findChild<QLabel*>("network-ipv4-provider")->text(),QString("Unavailable"));
 }
 void changedIpRejectsDelayedReplyAndSessionCloseCancels(){
  Dns dns;dns.delay=true;app::NetworkIdentityController owner(nullptr,QHostAddress::LocalHost,dns.socket.localPort(),2000);owner.beginSession(1);QSignalSpy spy(&owner,&app::NetworkIdentityController::ready);owner.observe(input(1));QTRY_COMPARE(dns.queries.size(),1);owner.observe(input(2,"8.8.4.4"));QTRY_VERIFY_WITH_TIMEOUT(last(spy)->network->families[0].asn&&last(spy)->network->families[0].asn->name=="Generated Provider B",3000);QCOMPARE(last(spy)->network->families[0].address,QString("8.8.4.4"));for(const auto& packet:spy){const auto value=qvariant_cast<ui::RuntimeMetricsPresentation>(packet.first());if(value->network->families[0].address=="8.8.4.4"&&value->network->families[0].asn){QCOMPARE(value->network->families[0].asn->number,QString("64501"));}}
  owner.observe(input(3,"8.8.8.8"));owner.beginSession(0);const auto count=spy.size();QTest::qWait(200);QCOMPARE(spy.size(),count);owner.beginSession(2);owner.observe(input(5));QCOMPARE(spy.size(),count);
 }
 void timeoutAndPolicyDoNotSpinOrResolvePrivateAddresses(){
  Dns dns;dns.drop=true;app::NetworkIdentityController owner(nullptr,QHostAddress::LocalHost,dns.socket.localPort(),40);owner.beginSession(1);QSignalSpy spy(&owner,&app::NetworkIdentityController::ready);owner.observe(input(1));QTRY_VERIFY(last(spy)->network->families[0].status.contains("timed out"));for(int i=2;i<20;++i){owner.observe(input(i));}QCOMPARE(dns.queries.size(),1);owner.observe(input(20,"8.8.4.4",{},false));owner.observe(input(21,"127.0.0.1","fd00::1"));QTest::qWait(80);QCOMPARE(dns.queries.size(),1);QVERIFY(last(spy)->network->families[0].address.isEmpty());
 }
 void matchingOriginalCacheNeedsNoLookupAndDoesNotMutateConfig(){
  Dns dns;app::NetworkIdentityController owner(nullptr,QHostAddress::LocalHost,dns.socket.localPort(),2000);owner.beginSession(1);QSignalSpy spy(&owner,&app::NetworkIdentityController::ready);auto packet=std::make_shared<ui::RuntimeMetricsPacket>(*input(1));auto info=std::make_shared<ni::Input>(*packet->networkInput);info->saved.push_back({"64500","Generated saved provider","8.8.8.0/24"});packet->networkInput=info;owner.observe(packet);QVERIFY(last(spy)->network->families[0].cached);QCOMPARE(last(spy)->network->families[0].asn->name,QString("Generated saved provider"));QCOMPARE(dns.queries.size(),0);
 }
 void originalJavaQueryAndParsingOracle(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_NETWORK_IDENTITY_JAVA")){QSKIP("Original Java oracle not configured");}QTemporaryDir root;QVERIFY(root.isValid());QProcess child;child.start(qEnvironmentVariable("BIGLYBTC_NETWORK_IDENTITY_JAVA"),{"-cp",qEnvironmentVariable("BIGLYBTC_NETWORK_IDENTITY_CLASSPATH"),"NetworkIdentityOracle",root.path()});QVERIFY(child.waitForStarted());QVERIFY(child.waitForFinished(10000));QCOMPARE(child.exitCode(),0);QFile file(root.path()+"/oracle.ben");QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);for(const auto& row:doc->find("rows")->children){const auto value=[&](const char* key){const auto* v=row.find(key);return QString::fromUtf8(v->bytes.data(),qsizetype(v->bytes.size()));};QHostAddress address(value("ip"));QCOMPARE(ni::originQuery(address),value("query"));const auto parsed=ni::parseOrigin(address,value("origin"));QVERIFY(parsed);QCOMPARE(parsed->number,value("as"));QCOMPARE(parsed->prefix,value("bgp"));const auto name=ni::parseName(parsed->number,value("name_record"));QVERIFY(name);QCOMPARE(*name,value("name"));}
 }
};
QTEST_MAIN(NetworkIdentityTests)
#include "network_identity_tests.moc"
