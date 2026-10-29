// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <zlib.h>
#include <algorithm>
namespace i2p=biglybtc::services::i2p;namespace cp=i2p::i2cp;using Channel=cp::InitialChannel;
namespace {
constexpr std::uint64_t Now=1761739200123ULL;
i2p::ClientIdentity signer(const char* family){
  auto read=[&](const char* suffix){const auto path=QString::fromUtf8(BIGLYBTC_I2P_TEST_KEYS_DIR)+QLatin1Char('/')+QString::fromLatin1(family)+QString::fromLatin1(suffix);QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()<1||file.size()>1024){qFatal("Cannot open bounded signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}const auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError){qFatal("Cannot read signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}return i2p::Bytes(reinterpret_cast<const i2p::Byte*>(bytes.constData()),reinterpret_cast<const i2p::Byte*>(bytes.constData())+bytes.size());};
  auto result=i2p::makeClientIdentity(read(".sender"),i2p::Bytes(256,0x51),read(".known-test-key"));if(!result){qFatal("Cannot import synthetic signer: %s",result.error().message.c_str());}return std::move(*result);
}
struct Router {
  QTcpServer server;QPointer<QTcpSocket> peer;QByteArray bytes;qsizetype offset=1;QString failure;
  Router(){QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{if(peer){failure="Unexpected second client";return;}peer=server.nextPendingConnection();QObject::connect(peer,&QTcpSocket::readyRead,&server,[this]{collect();});collect();});}
  ~Router(){QObject::disconnect(&server,nullptr,&server,nullptr);if(peer){QObject::disconnect(peer,nullptr,&server,nullptr);peer->abort();}}
  void collect(){const auto n=peer->bytesAvailable();if(n<0||n>131077-bytes.size()){failure="Unbounded owned router input";peer->abort();return;}bytes+=peer->read(n);}
  bool listen(bool ipv6=false){return server.listen(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0);}
  Channel::Options options(int budget=3000)const{return {server.serverAddress(),server.serverPort(),"2.13.0",false,budget};}
  bool send(i2p::View data){return peer&&peer->write(reinterpret_cast<const char*>(data.data()),static_cast<qint64>(data.size()))==static_cast<qint64>(data.size());}
  std::optional<cp::Frame> next()const{if(bytes.size()-offset<5){return std::nullopt;}const auto* at=reinterpret_cast<const cp::Byte*>(bytes.constData()+offset);std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|at[i];}if(n>cp::MaximumPayloadBytes){qFatal("Unbounded actual client frame");}if(bytes.size()-offset<5+static_cast<qsizetype>(n)){return std::nullopt;}auto decoded=cp::decodeFrame(cp::View(at,n+5));if(!decoded){qFatal("Malformed client frame: %s",decoded.error().message.c_str());}return std::move(*decoded);}
  cp::Frame take(){auto frame=next();if(!frame){qFatal("Missing actual client frame");}offset+=5+static_cast<qsizetype>(frame->payload.size());return std::move(*frame);}
  bool status(std::uint32_t nonce,std::uint32_t message,cp::Byte code,std::uint16_t session=7){auto data=cp::encodeMessageStatus({session,message,code,0,nonce});return data&&send(*data);}
  bool lease(std::uint64_t expiry=Now+600000){cp::RequestedLease l;l.gateway.fill(0x51);l.tunnelId=33;l.expiresMilliseconds=expiry;auto data=cp::encodeLeaseRequest({7,{l}});return data&&send(*data);}
};
}

namespace {
QByteArray words(std::initializer_list<std::uint32_t> values){QByteArray out;for(auto word:values)for(int i=3;i>=0;--i){out.append(static_cast<char>(word>>(8*i)));}return out;}
std::uint32_t word(const i2p::Bytes& p,std::size_t at){if(p.size()<at+4){qFatal("Short independent tracker packet");}std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|p[at+i];}return n;}
i2p::Bytes rawGzip(const QByteArray& bytes,std::uint16_t from=6969,std::uint16_t to=9000){
  z_stream stream{};if(deflateInit2(&stream,3,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){qFatal("Owned gzip init failed");}i2p::Bytes out(2048);stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData()));stream.avail_in=static_cast<uInt>(bytes.size());stream.next_out=out.data();stream.avail_out=static_cast<uInt>(out.size());if(deflate(&stream,Z_FINISH)!=Z_STREAM_END){qFatal("Owned gzip failed");}out.resize(stream.total_out);deflateEnd(&stream);if(out.size()<18){qFatal("Short owned gzip");}out.at(4)=from>>8;out.at(5)=static_cast<i2p::Byte>(from);out.at(6)=to>>8;out.at(7)=static_cast<i2p::Byte>(to);out.at(9)=18;return out;
}
}
class TrackerChannelTests final:public QObject {
  Q_OBJECT
  static constexpr std::uint64_t Connection=0x123456789abcdef0ULL;
  void registered(Router& router,Channel& channel,bool fast,int budget=3000,bool provideLease=true){
    QVERIFY(channel.begin(router.options(budget)));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);QCOMPARE(router.bytes.at(0),'*');auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());auto id=signer("ed25519");QVERIFY(channel.createLeaseSession(std::move(id),{{"i2cp.fastReceive",fast?"true":"false"},{"i2cp.messageReliability","none"}},Now));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());if(!provideLease){return;}QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());QVERIFY(!channel.receiverObservation().enabled);
  }
  cp::TrackerRequest request(int budget=3000){auto peer=signer("dsa");cp::TrackerRequest out;out.destination=peer.destination()->hash;out.infoHash.fill(0x51);out.port=6969;out.deadlineMilliseconds=budget;return out;}
  cp::HostLookup lookup(Router& router){auto value=cp::decodeHostLookup(router.take());if(!value){qFatal("Actual lookup malformed: %s",value.error().message.c_str());}return *value;}
  void answer(Router& router,const cp::HostLookup& lookup,std::uint16_t sid=7){auto peer=signer("dsa");auto reply=cp::encodeHostReply({sid,lookup.request,0,*peer.destination()});QVERIFY(reply&&router.send(*reply));}
  cp::SendDatagram sent(Router& router){auto parsed=cp::decodeSendDatagram(router.take());if(!parsed){qFatal("Actual tracker send malformed: %s",parsed.error().message.c_str());}return std::move(*parsed);}
  i2p::Bytes body(const cp::SendDatagram& send,bool connecting){
    auto local=signer("ed25519"),remote=signer("dsa");if(send.sessionId!=7||send.nonce!=0||send.recipient.wire!=remote.destination()->wire||!send.expirationMilliseconds||send.skipLeaseSet==connecting){qFatal("Wrong exact tracker envelope/skipLS policy");}auto mux=cp::decodeMuxDatagram(send.payload);if(!mux||mux->protocol!=(connecting?19:20)||mux->fromPort!=9000||mux->toPort!=6969){qFatal("Wrong signed/claimed tracker protocol/ports");}if(connecting){auto d=i2p::decode2(mux->datagram,remote.destination()->hash);if(!d||d->sender.wire!=local.destination()->wire){qFatal("Wrong actual connect signing identity/recipient");}return std::move(d->payload);}auto d=i2p::decode3(mux->datagram);if(!d||d->claimedSender!=local.destination()->hash){qFatal("Wrong claimed scrape sender");}return std::move(d->payload);
  }
  std::uint32_t connectRequest(Router& router){const auto data=body(sent(router),true);if(data.size()!=16||word(data,0)!=0x417||word(data,4)!=0x27101980||word(data,8)!=0){qFatal("Wrong independent BEP15 connect");}return word(data,12);}
  std::uint32_t scrapeRequest(Router& router){const auto data=body(sent(router),false);if(data.size()!=36||word(data,0)!=(Connection>>32)||word(data,4)!=static_cast<std::uint32_t>(Connection)||word(data,8)!=2||!std::all_of(data.begin()+16,data.end(),[](auto b){return b==0x51;})){qFatal("Wrong actual connection/hash-scoped scrape");}return word(data,12);}
  void deliver(Router& router,bool fast,std::uint32_t id,const QByteArray& packet,std::uint16_t from=6969,std::uint16_t to=9000){
    auto gzip=rawGzip(packet,from,to);if(!fast){auto available=cp::encodeMessageStatus({7,id,0,static_cast<std::uint32_t>(gzip.size()),1});QVERIFY(available&&router.send(*available));QTRY_VERIFY(router.next());auto begin=cp::decodeReceiveControl(router.take());QVERIFY(begin&&begin->begin&&begin->sessionId==7&&begin->messageId==id);}auto payload=cp::encodeMessagePayload({7,id,gzip});QVERIFY(payload&&router.send(*payload));if(!fast){QTRY_VERIFY(router.next());auto end=cp::decodeReceiveControl(router.take());QVERIFY(end&&!end->begin&&end->sessionId==7&&end->messageId==id);}
  }
  void counts(Channel& channel,std::uint64_t operation){QTRY_VERIFY(channel.trackerObservation()&&channel.trackerObservation()->phase==cp::TrackerPhase::Counts);auto result=channel.trackerObservation();QVERIFY(result&&result->operation==operation&&result->counts);QCOMPARE(result->counts->seeds,std::int32_t(3));QCOMPARE(result->counts->downloaded,std::int32_t(4));QCOMPARE(result->counts->peers,std::int32_t(5));QCOMPARE(channel.receiverObservation().queued,std::size_t(0));QVERIFY(!channel.receiverObservation().enabled);}
private slots:
  void actualHashLookupSignedConnectClaimedScrapeRawReply_data(){QTest::addColumn<bool>("fast");QTest::addColumn<bool>("ipv6");QTest::newRow("legacy-v4")<<false<<false;QTest::newRow("fast-v4")<<true<<false;QTest::newRow("legacy-v6")<<false<<true;QTest::newRow("fast-v6")<<true<<true;}
  void actualHashLookupSignedConnectClaimedScrapeRawReply(){
    QFETCH(bool,fast);QFETCH(bool,ipv6);Router router;QVERIFY(router.listen(ipv6));Channel channel;registered(router,channel,fast);QVERIFY(channel.leaseSetSubmission());const auto req=request();auto operation=channel.scrapeTracker(req);QVERIFY(operation);QTRY_VERIFY(router.next());const auto query=lookup(router);QVERIFY(query.sessionId==7&&query.hash==req.destination&&query.timeoutMilliseconds<=3000);answer(router,query);QTRY_VERIFY(router.next());const auto connect=connectRequest(router);deliver(router,fast,101,words({0,connect,static_cast<std::uint32_t>(Connection>>32),static_cast<std::uint32_t>(Connection)}));QTRY_VERIFY(router.next());const auto scrape=scrapeRequest(router);QVERIFY(connect!=scrape);deliver(router,fast,102,words({2,scrape,3,4,5}));counts(channel,*operation);auto next=channel.scrapeTracker(req);QVERIFY(next&&*next!=*operation);QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::HostLookupType);QVERIFY(channel.cancelTracker(*next));QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(router.failure.isEmpty());
  }
  void wrongPortsTransactionsAndOldLookupCannotFinishCurrentOperation(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,true);auto req=request();auto old=channel.scrapeTracker(req);QVERIFY(old);QTRY_VERIFY(router.next());const auto stale=lookup(router);QVERIFY(channel.cancelTracker(*old));auto current=channel.scrapeTracker(req);QVERIFY(current);QTRY_VERIFY(router.next());const auto fresh=lookup(router);QVERIFY(stale.request!=fresh.request);answer(router,stale);auto malformed=cp::encodeHostReply({7,stale.request,1,std::nullopt});QVERIFY(malformed);malformed->push_back(0);(*malformed)[3]=8;QVERIFY(router.send(*malformed));answer(router,fresh,8);answer(router,fresh);QTRY_VERIFY(router.next());const auto transaction=connectRequest(router);const auto valid=words({0,transaction,static_cast<std::uint32_t>(Connection>>32),static_cast<std::uint32_t>(Connection)});deliver(router,true,201,valid,6970);deliver(router,true,202,valid,6969,9001);deliver(router,true,203,words({0,transaction^1,0,1}));deliver(router,true,204,valid);QTRY_VERIFY(router.next());const auto scrape=scrapeRequest(router);deliver(router,true,205,words({2,scrape,3,4,5}));counts(channel,*current);QVERIFY(channel.trackerObservation()->rejectedReplies>=6);
  }
  void cancellationAndLateRawReplyCannotCompleteNextOperation(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,true);auto old=channel.scrapeTracker(request());QVERIFY(old);QTRY_VERIFY(router.next());answer(router,lookup(router));QTRY_VERIFY(router.next());const auto a=connectRequest(router);deliver(router,true,301,words({0,a,static_cast<std::uint32_t>(Connection>>32),static_cast<std::uint32_t>(Connection)}));QTRY_VERIFY(router.next());const auto staleScrape=scrapeRequest(router);QVERIFY(channel.cancelTracker(*old));auto next=channel.scrapeTracker(request());QVERIFY(next);QTRY_VERIFY(router.next());answer(router,lookup(router));QTRY_VERIFY(router.next());const auto b=connectRequest(router);QVERIFY(b!=a&&b!=staleScrape);deliver(router,true,302,words({2,staleScrape,99,99,99}));deliver(router,true,303,words({0,b,static_cast<std::uint32_t>(Connection>>32),static_cast<std::uint32_t>(Connection)}));QTRY_VERIFY(router.next());const auto scrape=scrapeRequest(router);QVERIFY(scrape!=a&&scrape!=staleScrape&&scrape!=b);deliver(router,true,304,words({2,scrape,3,4,5}));counts(channel,*next);
  }
  void actualLookupFailureMismatchAndDeadlineAreLocalAndReusable_data(){QTest::addColumn<int>("kind");QTest::newRow("routerfailure")<<0;QTest::newRow("wrongdestination")<<1;QTest::newRow("lookup-timeout")<<2;}
  void actualLookupFailureMismatchAndDeadlineAreLocalAndReusable(){
    QFETCH(int,kind);Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,true);auto operation=channel.scrapeTracker(request(kind==2?100:3000));QVERIFY(operation);QTRY_VERIFY(router.next());const auto query=lookup(router);if(kind==0){auto failure=cp::encodeHostReply({7,query.request,6,std::nullopt});QVERIFY(failure&&router.send(*failure));}if(kind==1){auto wrong=signer("ed25519");auto reply=cp::encodeHostReply({7,query.request,0,*wrong.destination()});QVERIFY(reply&&router.send(*reply));}const auto expected=kind==0?cp::TrackerPhase::LookupFailed:kind==1?cp::TrackerPhase::DestinationMismatch:cp::TrackerPhase::LocalTimeout;QTRY_VERIFY(channel.trackerObservation()&&channel.trackerObservation()->phase==expected);QVERIFY(!channel.trackerObservation()->counts);auto next=channel.scrapeTracker(request());QVERIFY(next);QTRY_VERIFY(router.next());answer(router,query);const auto fresh=lookup(router);answer(router,fresh);QTRY_VERIFY(router.next());(void)connectRequest(router);QVERIFY(channel.cancelTracker(*next));QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void createdWithoutLeaseRefusesAndCallbacksMayDelete(){
    Router router;QVERIFY(router.listen());QPointer<Channel> channel=new Channel(&router.server);QVERIFY(!channel->scrapeTracker(request()));registered(router,*channel,true,3000,false);QVERIFY(channel->sessionId());QVERIFY(!channel->scrapeTracker(request()));QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel->leaseSetSubmission());auto operation=channel->scrapeTracker(request());QVERIFY(operation);QTRY_VERIFY(router.next());answer(router,lookup(router));QTRY_VERIFY(router.next());const auto transaction=connectRequest(router);QObject::connect(channel,&Channel::trackerChanged,&router.server,[&]{if(channel&&channel->trackerObservation()&&channel->trackerObservation()->phase==cp::TrackerPhase::TrackerError){delete channel.data();}});deliver(router,true,401,words({3,transaction}));QTRY_VERIFY(channel.isNull());QTRY_COMPARE(router.peer->state(),QAbstractSocket::UnconnectedState);
  }
};
QTEST_GUILESS_MAIN(TrackerChannelTests)
#include "i2cp_tracker_channel_tests.moc"
