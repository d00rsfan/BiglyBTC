// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_owner_fixture.hpp"
#include "owned_transport.hpp"
#include "azureus_local_diversification.hpp"
#include "azureus_remote_diversification.hpp"
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QScopeGuard>
#include <QTest>
#include <future>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace nodes=services::azureus_nodes;
namespace {
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()){throw std::runtime_error("Generated owner checkpoint fixture");}}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){return {};}return f.readAll();}
nodes::Contact contact(udp::Peer peer){nodes::ResolvedEndpoint ep;ep.address.length=4;ep.address.port=peer.port;auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){ep.address.bytes[i]=std::uint8_t(ip>>(24-8*i));}nodes::NodeCache cache;auto id=cache.lookup(ep,55);if(!id){throw std::runtime_error("Generated contact");}nodes::Contact c;c.protocol=55;c.address=ep.address;c.id=id->id;return c;}
}
class AzureusLocalCheckpointOwnerTests final:public QObject {
 Q_OBJECT
private slots:
 void originalLocalCheckpointRoundTrip_data(){QTest::addColumn<bool>("failSave");QTest::newRow("durable-restart")<<false;QTest::newRow("pending-file-save-refused")<<true;}
 void originalLocalCheckpointRoundTrip(){
  QFETCH(bool,failSave);udp::Config seedConfig;seedConfig.network=4;seedConfig.servePings=true;udp::OwnedPingTransport seed(std::move(seedConfig));QVERIFY(seed.start(QHostAddress("127.121.41.1")));
  generated_chat::GeneratedClient target(QHostAddress("127.122.41.1"),seed.endpoint());
  QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);
  const auto path=target.root.path()+"/dht/net4/diverse.dat";const auto pending=target.root.path()+"/dht/net4/diverse.saving";
  const auto expiry=QDateTime::currentMSecsSinceEpoch()+240000000;
  const auto original="d5:locall4:keepe6:remoteld3:expi"+QByteArray::number(expiry)+"e3:fpoli2ei7ee3:key20:rrrrrrrrrrrrrrrrrrrr4:typei3eee7:unknown4:keepe";
  write(path,original);if(failSave){write(pending,"de");}
  udp::Config config;config.network=4;udp::OwnedPingTransport caller(std::move(config));QVERIFY(caller.start(QHostAddress("127.123.41.1")));const udp::Peer destination{QHostAddress("127.122.41.1"),target.port};QVERIFY(caller.allowPeer(destination));
  std::optional<udp::DetailedFindNodeOutcome> token;QVERIFY(caller.requestStoreToken(destination,55,this,[&](auto value){token.emplace(std::move(value));},2s));QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(*token);const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);const auto cookie=std::get<udp::find::ReplyBody>(decoded->body()).randomId;
  const auto expectedKey=contact(destination).id;const std::string key(reinterpret_cast<const char*>(expectedKey.data()),expectedKey.size());
  std::vector<std::unique_ptr<udp::OwnedPingTransport>> senders;
  auto store=[&](unsigned n){
   udp::OwnedPingTransport* sender=&caller;auto tokenValue=cookie;
   if(n){udp::Config next;next.network=4;auto owned=std::make_unique<udp::OwnedPingTransport>(std::move(next));if(!owned->start(QHostAddress(QString("127.%1.61.1").arg(n+30)))||!owned->allowPeer(destination)){throw std::runtime_error("Generated direct origin");}sender=owned.get();senders.push_back(std::move(owned));auto tokenReply=std::make_shared<std::optional<udp::DetailedFindNodeOutcome>>();if(!sender->requestStoreToken(destination,55,this,[tokenReply](auto value){tokenReply->emplace(std::move(value));},1s)){throw std::runtime_error("Generated token request");}QDeadlineTimer end(1500);while(!tokenReply->has_value()&&!end.hasExpired()){QTest::qWait(1);}if(!tokenReply->has_value()||!**tokenReply){throw std::runtime_error("Generated direct token");}auto* received=std::get_if<udp::find::Decoded>(&***tokenReply);if(!received){throw std::runtime_error("Generated token response");}tokenValue=std::get<udp::find::ReplyBody>(received->body()).randomId;}
   auto output=std::make_shared<std::optional<udp::StoreOutcome>>();udp::StoreRequest request;request.randomId=tokenValue;request.keys={key};request.valueSets={{{1,QDateTime::currentMSecsSinceEpoch(),std::string(512,'v'),contact(sender->endpoint()),0,0,-1}}};auto ticket=sender->store(destination,55,std::move(request),this,[output](auto value){output->emplace(std::move(value));},1500ms);if(!ticket){throw std::runtime_error("Generated owner STORE "+std::to_string(int(ticket.error())));}return output;
  };
  // Original unseeded nodes intentionally ignore forwarded values. Use65 real
  // direct senders, each with an issued token and distinct restricted node ID.
  for(unsigned n=0;n<64;++n){auto output=store(n);QTRY_VERIFY_WITH_TIMEOUT(output->has_value(),2500);QVERIFY2(**output,"Initial non-diversified store failed");}
  auto final=store(64);QTRY_VERIFY_WITH_TIMEOUT(final->has_value(),3000);
  if(failSave){QVERIFY(!**final);QCOMPARE(read(path),original);QCOMPARE(read(pending),QByteArray("de"));}
  else{QVERIFY(**final);const auto* packet=std::get_if<udp::storage::Decoded>(&***final);QVERIFY(packet);const auto* reply=std::get_if<udp::storage::ReplyBody>(&packet->body());QVERIFY(reply&&reply->diversification);QCOMPARE(*reply->diversification,std::string(1,char(3)));auto doc=core::Document::parse(read(path).toStdString());QVERIFY(doc);QCOMPARE(doc->find("unknown")->bytes,std::string_view("keep"));QVERIFY(!QFile::exists(pending));auto before=core::Document::parse(original.toStdString());QVERIFY(before);auto actualRemote=core::encodeValue(*doc->find("remote"));auto originalRemote=core::encodeValue(*before->find("remote"));QVERIFY(actualRemote&&originalRemote);QCOMPARE(*actualRemote,*originalRemote);QVERIFY(doc->find("local")->children[0].bytes=="keep");}
  auto prepared=std::async(std::launch::async,[&]{return target.chats->prepareStop(std::chrono::steady_clock::now()+750ms);});QTRY_VERIFY_WITH_TIMEOUT(prepared.wait_for(0ms)==std::future_status::ready,1500);QCOMPARE(prepared.get(),!failSave);
  target.chats->requestStop();target.chats->join();target.chats.reset();
  auto versions=storage::AzureusValueVersions::open(*target.lease,4);QVERIFY(versions);
  if(failSave){QVERIFY(QFile::remove(pending));auto records=storage::readAzureusLocalDiversifications(**versions,QDateTime::currentMSecsSinceEpoch());QVERIFY(records&&records->empty());return;}
  auto records=storage::readAzureusLocalDiversifications(**versions,QDateTime::currentMSecsSinceEpoch());QVERIFY(records&&records->size()==1);QCOMPARE((*records)[0].type,std::int8_t{3});QCOMPARE((*records)[0].key,expectedKey);
  const auto saved=read(path);auto factory=engine::ChatTransportFactory::owned(*target.sdk,*target.lease,target.port);QVERIFY(factory);auto service=app::GeneralChats::create(std::move(*factory),2);QVERIFY(service);target.chats=std::move(*service);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);
  // The original local type/expiry survives while all volatile values were lost.
  // Target the receiver itself so restored routing still admits a new key.
  token.reset();QVERIFY(caller.requestStoreToken(destination,55,this,[&](auto value){token.emplace(std::move(value));},2s));QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(*token);decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);const auto restoredCookie=std::get<udp::find::ReplyBody>(decoded->body()).randomId;
  udp::StoreRequest request;request.randomId=restoredCookie;request.keys={key};request.valueSets={{{2,QDateTime::currentMSecsSinceEpoch(),"small restarted value",contact(caller.endpoint()),0,0,-1}}};std::optional<udp::StoreOutcome> reply;QVERIFY(caller.store(destination,55,std::move(request),this,[&](auto value){reply.emplace(std::move(value));},2s));QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),3000);QVERIFY(*reply);const auto* packet=std::get_if<udp::storage::Decoded>(&**reply);QVERIFY(packet);const auto* body=std::get_if<udp::storage::ReplyBody>(&packet->body());QVERIFY(body&&body->diversification);QCOMPARE(*body->diversification,std::string(1,char(3)));QCOMPARE(read(path),saved);
 }
};
QTEST_GUILESS_MAIN(AzureusLocalCheckpointOwnerTests)
#include "azureus_local_checkpoint_owner_tests.moc"
