// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
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
class DatagramChannelTests final:public QObject {
  Q_OBJECT
  void registered(Router& router,Channel& channel,const char* family,const cp::SessionOptions& options={},int budget=3000,std::uint64_t expiry=Now+600000){
    auto identity=signer(family);QVERIFY(channel.begin(router.options(budget)));QTRY_VERIFY(router.next());QCOMPARE(router.bytes[0],'*');QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());QVERIFY(channel.createLeaseSession(std::move(identity),options,Now));QVERIFY(!identity.valid());QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto status=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(status&&router.send(*status));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease(expiry));QTRY_VERIFY(router.next());auto created=cp::decodeCreateLeaseSet2(router.take());QVERIFY2(created.has_value(),created?"":created.error().message.c_str());QVERIFY(cp::verifyLeaseSet2(created->leaseSet));QTRY_VERIFY(channel.leaseSetSubmission());
  }
private slots:
  void periodicRouterDatesPreserveTheOriginalSessionLeaseAndActualDatagram(){
    Router router;QVERIFY(router.listen());Channel channel;
    QSignalSpy dates(&channel,&Channel::dateReceived),created(&channel,&Channel::sessionCreated),leases(&channel,&Channel::leaseSetSubmitted);
    registered(router,channel,"ed25519");QTRY_COMPARE(dates.count(),1);QTRY_COMPARE(created.count(),1);QTRY_COMPARE(leases.count(),1);
    const auto initialDate=channel.routerDate();const auto initialSession=channel.sessionId();const auto initialOptions=channel.sessionOptions();const auto initialLease=channel.leaseSetSubmission();
    QVERIFY(initialDate&&initialSession&&initialLease);QVERIFY(!channel.sourceEffectsPending());
    auto recipient=signer("dsa");const auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());QVERIFY(cp::decodeSendDatagram(router.take()));
    // Both a far-future and older valid router notification must leave the
    // original clock/capability/lease fence untouched, without a new identity.
    const auto forward=cp::encodeMessage(cp::SetDate{Now+7200000,std::string("0.9.70")});
    const auto backward=cp::encodeMessage(cp::SetDate{Now-600000,std::nullopt});QVERIFY(forward&&backward);
    QVERIFY(router.send(*forward));QVERIFY(router.send(*backward));QVERIFY(router.status(*sent,123,6));
    QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.state(),Channel::State::SessionCreated);QCOMPARE(channel.failure(),Channel::Failure::None);
    const auto outcome=channel.datagramObservation();QVERIFY(outcome&&outcome->routerStatus);QCOMPARE(outcome->nonce,*sent);QCOMPARE(outcome->routerStatus->code,cp::Byte(6));
    QVERIFY(channel.routerDate()==initialDate);QVERIFY(channel.sessionId()==initialSession);QVERIFY(channel.sessionOptions()==initialOptions);
    const auto lease=channel.leaseSetSubmission();QVERIFY(lease);QCOMPARE(lease->revision,initialLease->revision);QCOMPARE(lease->publishedMilliseconds,initialLease->publishedMilliseconds);QCOMPARE(lease->expiresMilliseconds,initialLease->expiresMilliseconds);QCOMPARE(lease->leases,initialLease->leases);QCOMPARE(lease->unpublished,initialLease->unpublished);
    QCOMPARE(dates.count(),1);QCOMPARE(created.count(),1);QCOMPARE(leases.count(),1);QVERIFY(!channel.deadlineActive());QVERIFY(!channel.sourceEffectsPending());QVERIFY(!router.next());
    QVERIFY(router.send(*forward));channel.close();QCoreApplication::processEvents();
    QCOMPARE(channel.state(),Channel::State::Closed);QVERIFY(!channel.routerDate());QVERIFY(!channel.sessionId());QVERIFY(!channel.leaseSetSubmission());QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));QCOMPARE(dates.count(),1);QCOMPARE(created.count(),1);QCOMPARE(leases.count(),1);
  }
  void actualSignedAndClaimedDatagramsExposeHonestRouterStatuses_data(){
    QTest::addColumn<QByteArray>("family");QTest::addColumn<bool>("ipv6");QTest::addColumn<bool>("claimed");
    QTest::newRow("DSA0-v4-signed")<<QByteArray("dsa")<<false<<false;QTest::newRow("Ed7-v6-signed")<<QByteArray("ed25519")<<true<<false;
    QTest::newRow("DSA0-v6-claimed")<<QByteArray("dsa")<<true<<true;QTest::newRow("Ed7-v4-claimed")<<QByteArray("ed25519")<<false<<true;
  }
  void actualSignedAndClaimedDatagramsExposeHonestRouterStatuses(){
    QFETCH(QByteArray,family);QFETCH(bool,ipv6);QFETCH(bool,claimed);Router router;QVERIFY(router.listen(ipv6));Channel channel;registered(router,channel,family.constData());QVERIFY(channel.leaseSetSubmission());auto recipient=signer(family=="dsa"?"ed25519":"dsa");const i2p::Bytes payload{0,0xff,0x51,3};Channel::DatagramOptions options;options.fromPort=65535;options.toPort=42;options.gzip=claimed?cp::GzipMode::Stored:cp::GzipMode::Deflate;if(!claimed){options.expirationMilliseconds=Now+60000;}
    auto sent=claimed?channel.submitClaimedDatagram3(recipient.destination()->wire,payload,options):channel.submitDatagram2(recipient.destination()->wire,payload,options);QVERIFY2(sent.has_value(),sent?"":sent.error().message.c_str());QVERIFY(channel.datagramPending());QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,payload,{}));QVERIFY(!channel.reconfigureLeaseSession({},Now));QTRY_VERIFY(router.next());auto actual=cp::decodeSendDatagram(router.take());QVERIFY(actual);QCOMPARE(actual->nonce,*sent);QCOMPARE(actual->sessionId,std::uint16_t(7));QVERIFY(actual->recipient.wire==recipient.destination()->wire);QVERIFY(actual->expirationMilliseconds==options.expirationMilliseconds);auto mux=cp::decodeMuxDatagram(actual->payload);QVERIFY(mux);QCOMPARE(mux->fromPort,std::uint16_t(65535));QCOMPARE(mux->toPort,std::uint16_t(42));
    if(claimed){auto decoded=i2p::decode3(mux->datagram);QVERIFY(decoded);QVERIFY(decoded->payload==payload);QVERIFY(decoded->claimedSender==signer(family.constData()).destination()->hash);}else{auto decoded=i2p::decode2(mux->datagram,recipient.destination()->hash);QVERIFY(decoded);QVERIFY(decoded->payload==payload);auto wrong=recipient.destination()->hash;wrong[0]^=1;QVERIFY(!i2p::decode2(mux->datagram,wrong));}
    QVERIFY(router.status(*sent,123,1));QTRY_VERIFY(channel.datagramObservation()&&channel.datagramObservation()->phase==Channel::DatagramPhase::RouterAccepted);QVERIFY(channel.datagramPending());QCOMPARE(channel.datagramObservation()->routerStatus->code,cp::Byte(1));QVERIFY(router.status(*sent,123,claimed?6:4));QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->phase,Channel::DatagramPhase::RouterStatus);QCOMPARE(channel.datagramObservation()->routerStatus->code,cp::Byte(claimed?6:4));QVERIFY(router.failure.isEmpty());
  }
  void createdWithoutActualLeaseCannotSubmit(){
    Router router;QVERIFY(router.listen());Channel channel;auto identity=signer("dsa"),recipient=signer("ed25519");QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());router.take();auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));QVERIFY(channel.createLeaseSession(std::move(identity),{},Now));QTRY_VERIFY(router.next());router.take();auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());QVERIFY(!channel.leaseSetSubmission());QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));channel.close();QVERIFY(!channel.datagramObservation());
  }
  void localCancelAndOldNonceCannotFinishTheNextRequest(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519");QVERIFY(channel.leaseSetSubmission());auto recipient=signer("dsa");auto first=channel.submitDatagram2(recipient.destination()->wire,{},{ });QVERIFY(first);QTRY_VERIFY(router.next());router.take();QVERIFY(channel.cancelDatagram(*first));QVERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->phase,Channel::DatagramPhase::LocalCancelled);QVERIFY(!channel.datagramObservation()->routerStatus);
    auto second=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(second&&*second>*first);QTRY_VERIFY(router.next());router.take();QVERIFY(!channel.cancelDatagram(*first));QVERIFY(router.status(*first,77,6));QVERIFY(router.status(*second,88,1));QTRY_VERIFY(channel.datagramObservation()&&channel.datagramObservation()->phase==Channel::DatagramPhase::RouterAccepted);QCOMPARE(channel.datagramObservation()->nonce,*second);QVERIFY(channel.datagramPending());
    QVERIFY(router.status(*second,89,6));QTRY_COMPARE(channel.state(),Channel::State::Failed);QVERIFY(!channel.datagramPending());QVERIFY(!channel.datagramObservation());QVERIFY(!channel.sessionId());
  }
  void terminalFailureCanPrecedeAcceptedAndOldStatusesCannotRebind(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"dsa");QVERIFY(channel.leaseSetSubmission());auto recipient=signer("ed25519");auto first=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(first);QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*first,55,15));QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->routerStatus->code,cp::Byte(15));auto next=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(next);QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*first,55,1));QVERIFY(router.status(*next,0,1));QTRY_VERIFY(channel.datagramObservation()&&channel.datagramObservation()->phase==Channel::DatagramPhase::RouterAccepted);QCOMPARE(channel.datagramObservation()->nonce,*next);QVERIFY(router.status(*next,0,6));QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->routerStatus->code,cp::Byte(6));
  }
  void renewalCannotExtendOrCancelTheIndependentStatusBudget(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"dsa",{},500);QVERIFY(channel.leaseSetSubmission());auto recipient=signer("ed25519");auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());router.take();QVERIFY(router.lease(Now+900000));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateLeaseSet2Type);QTRY_COMPARE(channel.leaseSetSubmission()->revision,std::uint64_t(2));QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->phase,Channel::DatagramPhase::LocalTimeout);QVERIFY(!channel.datagramObservation()->routerStatus);QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(channel.sessionId());
  }
  void actualLeaseExpiryWithdrawsSubmissionUntilNewSignedRenewal(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",{},5000,(Now/1000+2)*1000);QVERIFY(channel.leaseSetSubmission());auto recipient=signer("dsa");auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());router.take();QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->phase,Channel::DatagramPhase::LocalLeaseExpired);QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));QVERIFY(router.lease());QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateLeaseSet2Type);QTRY_COMPARE(channel.leaseSetSubmission()->revision,std::uint64_t(2));auto next=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(next&&*next>*sent);QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*sent,9,6));QVERIFY(router.status(*next,10,6));QTRY_VERIFY(!channel.datagramPending());QCOMPARE(channel.datagramObservation()->nonce,*next);QCOMPARE(channel.datagramObservation()->routerStatus->messageId,std::uint32_t(10));
  }
  void reliabilityPolicyRemainsTheInitialOriginalRouterSetting_data(){QTest::addColumn<bool>("none");QTest::newRow("none-cannot-upgrade")<<true;QTest::newRow("best-effort-cannot-downgrade")<<false;}
  void reliabilityPolicyRemainsTheInitialOriginalRouterSetting(){
    QFETCH(bool,none);Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"dsa",{{"i2cp.messageReliability",none?"NoNe":"BestEffort"}});QVERIFY(channel.leaseSetSubmission());auto recipient=signer("ed25519");if(none){QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));}
    QVERIFY(channel.reconfigureLeaseSession({{"i2cp.messageReliability",none?"BestEffort":"none"}},Now));QVERIFY(!channel.submitDatagram2(recipient.destination()->wire,{},{}));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::ReconfigureSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Updated});QVERIFY(ack&&router.send(*ack));QTRY_COMPARE(channel.state(),Channel::State::SessionCreated);auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent.has_value()!=none);if(sent){QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*sent,33,6));QTRY_VERIFY(!channel.datagramPending());}else{QVERIFY(!router.next());}
  }
  void wrongSessionStatusWithdrawsRatherThanCompletes(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"dsa");QVERIFY(channel.leaseSetSubmission());auto recipient=signer("ed25519");auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*sent,33,6,8));QTRY_COMPARE(channel.state(),Channel::State::Failed);QVERIFY(!channel.datagramObservation());
  }
  void publicDatagramNotificationMayDeleteTheChannel(){
    Router router;QVERIFY(router.listen());QPointer<Channel> channel=new Channel(&router.server);registered(router,*channel,"ed25519");QVERIFY(channel->leaseSetSubmission());auto recipient=signer("dsa");QObject::connect(channel,&Channel::datagramChanged,&router.server,[&]{if(channel&&channel->datagramObservation()&&channel->datagramObservation()->phase==Channel::DatagramPhase::RouterStatus){delete channel.data();}});auto sent=channel->submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());router.take();QVERIFY(router.status(*sent,33,6));QTRY_VERIFY(channel.isNull());QTRY_COMPARE(router.peer->state(),QAbstractSocket::UnconnectedState);
  }
};
QTEST_GUILESS_MAIN(DatagramChannelTests)
#include "i2cp_datagram_channel_tests.moc"
