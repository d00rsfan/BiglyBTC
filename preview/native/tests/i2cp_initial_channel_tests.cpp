// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QFile>
#include <QEventLoop>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <optional>

namespace wire=biglybtc::services::i2p::i2cp;
namespace i2p=biglybtc::services::i2p;
using Channel=wire::InitialChannel;
Q_DECLARE_METATYPE(Channel::Failure)

namespace {
i2p::ClientIdentity signer(const char* name){
  auto read=[&](const char* suffix){
    const auto path=QString::fromUtf8(BIGLYBTC_I2P_TEST_KEYS_DIR)+QLatin1Char('/')+QString::fromLatin1(name)+QString::fromLatin1(suffix);
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()<1||file.size()>1024){qFatal("Cannot open bounded synthetic signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}
    const auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError){qFatal("Cannot read synthetic signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}
    return i2p::Bytes(reinterpret_cast<const i2p::Byte*>(bytes.constData()),reinterpret_cast<const i2p::Byte*>(bytes.constData())+bytes.size());
  };
  const auto dest=read(".sender"),secret=read(".known-test-key");auto result=i2p::makeClientIdentity(dest,i2p::Bytes(256,0x51),secret);
  if(!result){qFatal("Cannot import synthetic signer: %s",result.error().message.c_str());}return std::move(*result);
}
QByteArray statusFrame(std::uint16_t id,wire::SessionState state){
  const auto bytes=wire::encodeMessage(wire::SessionStatus{id,state});if(!bytes){qFatal("Cannot encode original status fixture: %s",bytes.error().message.c_str());}
  return QByteArray(reinterpret_cast<const char*>(bytes->data()),static_cast<qsizetype>(bytes->size()));
}
std::optional<wire::Frame> sentFrame(const QByteArray& bytes,qsizetype offset){
  if(offset<0||bytes.size()-offset<5){return std::nullopt;}
  const auto* at=reinterpret_cast<const wire::Byte*>(bytes.constData()+offset);std::uint32_t length=0;
  for(unsigned i=0;i<4;++i){length=(length<<8)|at[i];}if(length>wire::MaximumPayloadBytes){qFatal("Unbounded client session frame");}
  if(bytes.size()-offset<5+static_cast<qsizetype>(length)){return std::nullopt;}
  auto parsed=wire::decodeFrame(wire::View(at,5+length));if(!parsed){qFatal("Client sent invalid session frame: %s",parsed.error().message.c_str());}return std::move(*parsed);
}
// Only generated, owned, random-port loopback endpoints. No real router.
struct OwnedRouter {
  QTcpServer server;
  QPointer<QTcpSocket> peer;
  QByteArray received;
  QString failure;
  int accepted=0;
  void collect(){
    const auto available=peer->bytesAvailable();
    if(available<0||available>4096-received.size()){
      failure=QStringLiteral("Unbounded client greeting");peer->abort();return;
    }
    received+=peer->read(available);
  }
  OwnedRouter(){
    QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
      if(peer||accepted){failure=QStringLiteral("Unexpected second connection");return;}
      peer=server.nextPendingConnection();++accepted;
      QObject::connect(peer,&QTcpSocket::readyRead,&server,[this]{collect();});
      collect(); // A pending connection can already have buffered client data.
    });
  }
  ~OwnedRouter(){
    QObject::disconnect(&server,nullptr,&server,nullptr);
    if(peer){QObject::disconnect(peer,nullptr,&server,nullptr);peer->abort();}
  }
  bool listen(const QHostAddress& address=QHostAddress::LocalHost){return server.listen(address,0);}
  Channel::Options options()const{
    return {server.serverAddress(),server.serverPort(),"2.13.0",false,5000};
  }
  bool send(const QByteArray& bytes){return peer&&peer->write(bytes)==bytes.size();}
};
}

class InitialChannelTests final:public QObject {
  Q_OBJECT
  QByteArray greeting,getDate,date,legacy,zero,disconnect,status;
  QByteArray fixture(const char* name){
    const QString path=QString::fromUtf8(BIGLYBTC_I2CP_CHANNEL_FIXTURE_DIR)+QLatin1Char('/')+QString::fromLatin1(name);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){qFatal("Cannot open I2CP fixture %s: %s",qPrintable(path),qPrintable(file.errorString()));}
    if(file.size()<1||file.size()>512){qFatal("Invalid bounded fixture size: %s",qPrintable(path));}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError){qFatal("Cannot read I2CP fixture %s: %s",qPrintable(path),qPrintable(file.errorString()));}
    return bytes;
  }
  QByteArray hello()const{return greeting+getDate;}
private slots:
  void initTestCase(){
    greeting=fixture("greeting.frame");getDate=fixture("get-date-version.frame");
    date=fixture("set-date-version.frame");legacy=fixture("set-date-legacy.frame");
    zero=fixture("set-date-zero.frame");disconnect=fixture("disconnect-control.frame");status=fixture("status-created.frame");
    QCOMPARE(greeting,QByteArray(1,'*'));QCOMPARE(getDate.size(),qsizetype(12));QCOMPARE(date.size(),qsizetype(20));
  }
  void actualJavaDateOverOwnedTcp_data(){
    QTest::addColumn<bool>("ipv6");QTest::newRow("ipv4")<<false;QTest::newRow("ipv6")<<true;
  }
  void actualJavaDateOverOwnedTcp(){
    QFETCH(bool,ipv6);OwnedRouter router;
    QVERIFY2(router.listen(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost),qPrintable(router.server.errorString()));
    Channel channel;QSignalSpy observed(&channel,&Channel::dateReceived);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());
    QCOMPARE(channel.state(),Channel::State::WaitingDate);QVERIFY(channel.deadlineActive());
    // Separate bounded writes exercise incremental framing without assuming
    // TCP packet boundaries. The final observation requires the complete frame.
    QVERIFY(router.send(date.left(1)));QTRY_COMPARE(router.peer->bytesToWrite(),qint64(0));
    QVERIFY(router.send(date.mid(1,5)));QTRY_COMPARE(router.peer->bytesToWrite(),qint64(0));
    QVERIFY(router.send(date.mid(6)));QTRY_COMPARE(observed.count(),1);
    QCOMPARE(channel.state(),Channel::State::DateReceived);QVERIFY(!channel.deadlineActive());QVERIFY(channel.socketActive());
    const auto value=channel.routerDate();QVERIFY(value);QCOMPARE(value->milliseconds,std::uint64_t(1761739200123));
    QVERIFY(value->version==std::optional<std::string>("2.13.0"));QVERIFY(router.failure.isEmpty());
    channel.close();QCOMPARE(channel.state(),Channel::State::Closed);QVERIFY(!channel.routerDate());QVERIFY(!channel.socketActive());
  }
  void invalidRepliesWithdrawAllObservation_data(){
    QTest::addColumn<QByteArray>("reply");QTest::addColumn<Channel::Failure>("failure");
    QTest::newRow("oversize-header")<<QByteArray::fromHex("0002000121")<<Channel::Failure::Framing;
    QTest::newRow("unknown-type")<<QByteArray::fromHex("000000007f")<<Channel::Failure::UnsupportedMessage;
    QTest::newRow("maximum-bounded-opaque-frame")<<(QByteArray::fromHex("000200007f")+QByteArray(128*1024,'\0'))<<Channel::Failure::UnsupportedMessage;
    QTest::newRow("unexpected-real-session-status")<<status<<Channel::Failure::UnexpectedMessage;
    QTest::newRow("invalid-date-length")<<QByteArray::fromHex("000000012100")<<Channel::Failure::Framing;
    QTest::newRow("real-Java-null-date")<<zero<<Channel::Failure::InvalidDate;
  }
  void invalidRepliesWithdrawAllObservation(){
    QFETCH(QByteArray,reply);QFETCH(Channel::Failure,failure);OwnedRouter router;QVERIFY(router.listen());
    Channel channel;QSignalSpy observed(&channel,&Channel::dateReceived);QSignalSpy errors(&channel,&Channel::failed);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(reply));
    QTRY_COMPARE(errors.count(),1);QCOMPARE(channel.state(),Channel::State::Failed);QCOMPARE(channel.failure(),failure);
    QVERIFY(!channel.routerDate());QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());QCOMPARE(observed.count(),0);
  }
  void truncatedFrameAtActualEofRefuses(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;QSignalSpy errors(&channel,&Channel::failed);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date.left(7)));
    router.peer->disconnectFromHost();QTRY_COMPARE(errors.count(),1);QCOMPARE(channel.failure(),Channel::Failure::Framing);
    QVERIFY(!channel.routerDate());QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());
  }
  void realDisconnectRevokesDateAndRetainsOnlyReason(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;QSignalSpy dates(&channel,&Channel::dateReceived);QSignalSpy errors(&channel,&Channel::failed);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_COMPARE(dates.count(),1);
    QVERIFY(router.send(disconnect));QTRY_COMPARE(errors.count(),1);QCOMPARE(channel.failure(),Channel::Failure::RouterDisconnected);
    QVERIFY(!channel.routerDate());QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());
    const auto reason=channel.routerDisconnect();QVERIFY(reason);QVERIFY(reason->reason==std::string("\0\tA\177",4));
  }
  void actualRemoteEofWithdrawsDate(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;QSignalSpy dates(&channel,&Channel::dateReceived);QSignalSpy errors(&channel,&Channel::failed);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_COMPARE(dates.count(),1);
    router.peer->disconnectFromHost();QTRY_COMPARE(errors.count(),1);QCOMPARE(channel.failure(),Channel::Failure::TransportClosed);
    QVERIFY(!channel.routerDate());QVERIFY(!channel.routerDisconnect());QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());
  }
  void noDateHitsAbsoluteDeadlineWithoutRetry(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto options=router.options();options.deadlineMilliseconds=1000;
    QSignalSpy errors(&channel,&Channel::failed);QVERIFY(channel.begin(options));QTRY_COMPARE(router.received,hello());
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,3000);QCOMPARE(channel.failure(),Channel::Failure::Deadline);
    QCOMPARE(router.accepted,1);QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());QVERIFY(!channel.routerDate());
  }
  void cancelPartialThenExplicitRestartUsesNewStream(){
    OwnedRouter first,second;QVERIFY(first.listen());QVERIFY(second.listen());Channel channel;QSignalSpy dates(&channel,&Channel::dateReceived);
    QVERIFY(channel.begin(first.options()));QTRY_COMPARE(first.received,hello());QVERIFY(first.send(date.left(7)));QTRY_COMPARE(first.peer->bytesToWrite(),qint64(0));
    channel.close();QCOMPARE(channel.state(),Channel::State::Closed);QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());QVERIFY(!channel.routerDate());
    QVERIFY(channel.begin(second.options()));QTRY_COMPARE(second.received,hello());QVERIFY(second.send(legacy));QTRY_COMPARE(dates.count(),1);
    const auto value=channel.routerDate();QVERIFY(value);QCOMPARE(value->milliseconds,std::uint64_t(0x0102030405060708ULL));QVERIFY(!value->version);
    QCOMPARE(first.accepted,1);QCOMPARE(second.accepted,1);
  }
  void activeBeginCannotReplaceCurrentAttempt(){
    OwnedRouter first,second;QVERIFY(first.listen());QVERIFY(second.listen());Channel channel;
    QVERIFY(channel.begin(first.options()));QTRY_COMPARE(first.received,hello());QVERIFY(!channel.begin(second.options()));
    QCOMPARE(channel.state(),Channel::State::WaitingDate);QVERIFY(first.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QCOMPARE(channel.routerDate()->milliseconds,std::uint64_t(1761739200123));QCOMPARE(first.accepted,1);QCOMPARE(second.accepted,0);
  }
  void stateListenerRestartCannotPublishOldDate(){
    OwnedRouter first,second;QVERIFY(first.listen());QVERIFY(second.listen());Channel channel;bool restarted=false,restartAccepted=false;
    QObject::connect(&channel,&Channel::stateChanged,&channel,[&](Channel::State state){
      if(state==Channel::State::DateReceived&&!restarted){restarted=true;channel.close();restartAccepted=channel.begin(second.options());}
    });
    QSignalSpy dates(&channel,&Channel::dateReceived);QVERIFY(channel.begin(first.options()));QTRY_COMPARE(first.received,hello());QVERIFY(first.send(date));
    QTRY_VERIFY(restarted);QVERIFY(restartAccepted);QTRY_COMPARE(second.received,hello());QCOMPARE(dates.count(),0);
    QVERIFY(second.send(legacy));QTRY_COMPARE(dates.count(),1);QCOMPARE(channel.routerDate()->milliseconds,std::uint64_t(0x0102030405060708ULL));
  }
  void publicNotificationMayDeleteController_data(){
    QTest::addColumn<int>("kind");QTest::newRow("date-state-listener")<<0;QTest::newRow("date-final-listener")<<1;QTest::newRow("failure-listener")<<2;
  }
  void publicNotificationMayDeleteController(){
    QFETCH(int,kind);OwnedRouter router;QVERIFY(router.listen());QPointer<Channel> channel=new Channel(&router.server);
    if(kind==2){QObject::connect(channel,&Channel::failed,&router.server,[&](Channel::Failure){delete channel.data();});}
    else if(kind==1){QObject::connect(channel,&Channel::dateReceived,&router.server,[&]{delete channel.data();});}
    else{QObject::connect(channel,&Channel::stateChanged,&router.server,[&](Channel::State state){if(state==Channel::State::DateReceived){delete channel.data();}});}
    QVERIFY(channel->begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(kind==2?zero:date));QTRY_VERIFY(channel.isNull());
    QTRY_COMPARE(router.peer->state(),QAbstractSocket::UnconnectedState);
  }
  void unsupportedOptionsRefuseBeforeAnyTcp(){
    OwnedRouter router;QVERIFY(router.listen());const auto good=router.options();Channel channel;
    auto check=[&](const Channel::Options& options,Channel::Failure failure){
      const bool accepted=channel.begin(options);return !accepted&&channel.state()==Channel::State::Failed&&channel.failure()==failure&&!channel.socketActive()&&!channel.deadlineActive()&&!channel.routerDate();
    };
    auto options=good;options.authenticationRequired=true;QVERIFY(check(options,Channel::Failure::UnsupportedAuthentication));
    options=good;options.port=0;QVERIFY(check(options,Channel::Failure::InvalidEndpoint));
    options=good;options.address=QHostAddress(QStringLiteral("192.0.2.1"));QVERIFY(check(options,Channel::Failure::InvalidEndpoint));
    options=good;options.address=QHostAddress::AnyIPv4;QVERIFY(check(options,Channel::Failure::InvalidEndpoint));
    options=good;options.clientVersion.clear();QVERIFY(check(options,Channel::Failure::InvalidVersion));
    options=good;options.clientVersion=std::string(256,'a');QVERIFY(check(options,Channel::Failure::InvalidVersion));
    options=good;options.clientVersion=std::string(1,static_cast<char>(0xff));QVERIFY(check(options,Channel::Failure::InvalidVersion));
    options=good;options.deadlineMilliseconds=0;QVERIFY(check(options,Channel::Failure::InvalidDeadline));
    options=good;options.deadlineMilliseconds=30001;QVERIFY(check(options,Channel::Failure::InvalidDeadline));
    QCOMPARE(router.accepted,0);QVERIFY(router.received.isEmpty());
    // Positive recovery uses the same instance after the final refusal; no
    // rejected option can have left live I/O/deadline behind.
    QVERIFY(channel.begin(good));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
  }
  void actualSignedRegistrationAndMergedReconfigure_data(){
    QTest::addColumn<QByteArray>("family");QTest::addColumn<bool>("ipv6");
    QTest::newRow("DSA0-v4")<<QByteArray("dsa")<<false;QTest::newRow("Ed7-v4")<<QByteArray("ed25519")<<false;
    QTest::newRow("DSA0-v6")<<QByteArray("dsa")<<true;QTest::newRow("Ed7-v6")<<QByteArray("ed25519")<<true;
  }
  void actualSignedRegistrationAndMergedReconfigure(){
    QFETCH(QByteArray,family);QFETCH(bool,ipv6);OwnedRouter router;QVERIFY(router.listen(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost));
    Channel channel;auto identity=signer(family.constData());const auto expectedDestination=identity.destination()->wire;
    QSignalSpy created(&channel,&Channel::sessionCreated),updated(&channel,&Channel::sessionUpdated);
    QVERIFY(!channel.createSession(identity,{},1761739200123)); // No date/TCP authority.
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    const wire::SessionOptions initial{{"keep","old"},{"change","old"},{"i2cp.tcp.host","client-only"}};
    auto requested=channel.createSession(identity,initial,1761739200123);QVERIFY2(requested.has_value(),requested?"":requested.error().message.c_str());
    QCOMPARE(channel.state(),Channel::State::Creating);QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(channel.deadlineActive());
    QVERIFY(!channel.createSession(identity,initial,1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto first=*sentFrame(router.received,hello().size());auto parsed=wire::decodeSessionMessage(first);QVERIFY(parsed);
    const auto& config=std::get<wire::CreateSession>(*parsed).config;QVERIFY(config.destination.wire==expectedDestination);
    QVERIFY(wire::verifySessionConfig(config,1761739200123));QVERIFY(config.options==wire::SessionOptions({{"change","old"},{"keep","old"}}));
    identity.clear();QVERIFY(!identity.valid()); // Controller retained no borrowed signer.
    QVERIFY(router.send(status));QTRY_COMPARE(created.count(),1);QCOMPARE(channel.state(),Channel::State::SessionCreated);
    QVERIFY(channel.sessionId()==std::optional<std::uint16_t>(0xabcd));QVERIFY(channel.sessionOptions()==config.options);QVERIFY(!channel.deadlineActive());
    identity=signer(family.constData());const auto old=channel.sessionOptions();const wire::SessionOptions patch{{"change","new"},{"added","value"}};
    requested=channel.reconfigureSession(identity,patch,1761739200123);QVERIFY2(requested.has_value(),requested?"":requested.error().message.c_str());
    QCOMPARE(channel.state(),Channel::State::Reconfiguring);QVERIFY(channel.sessionOptions()==old);QVERIFY(channel.deadlineActive());
    const auto offset=hello().size()+5+static_cast<qsizetype>(first.payload.size());QTRY_VERIFY(sentFrame(router.received,offset).has_value());
    auto update=wire::decodeSessionMessage(*sentFrame(router.received,offset));QVERIFY(update);const auto& changed=std::get<wire::ReconfigureSession>(*update);
    QCOMPARE(changed.sessionId,std::uint16_t(0xabcd));QVERIFY(changed.config.destination.wire==expectedDestination);QVERIFY(wire::verifySessionConfig(changed.config,1761739200123));
    QVERIFY(router.send(statusFrame(0xabcd,wire::SessionState::Updated)));QTRY_COMPARE(updated.count(),1);QCOMPARE(created.count(),1);
    QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(channel.sessionOptions()==wire::SessionOptions({{"added","value"},{"change","new"},{"keep","old"}}));
    router.peer->disconnectFromHost();QTRY_COMPARE(channel.state(),Channel::State::Failed);QCOMPARE(channel.failure(),Channel::Failure::TransportClosed);
    QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(!channel.routerDate());QVERIFY(!channel.deadlineActive());
  }
  void terminalRegistrationStatusNeverAdmits_data(){
    QTest::addColumn<int>("kind");QTest::addColumn<Channel::Failure>("failure");
    QTest::newRow("refused-UNKNOWN")<<4<<Channel::Failure::SessionRejected;
    QTest::newRow("invalid")<<3<<Channel::Failure::SessionRejected;
    QTest::newRow("destroyed")<<0<<Channel::Failure::SessionDestroyed;
    QTest::newRow("unsolicited-updated")<<2<<Channel::Failure::UnexpectedMessage;
    QTest::newRow("created-UNKNOWN")<<1<<Channel::Failure::InvalidSessionId;
  }
  void terminalRegistrationStatusNeverAdmits(){
    QFETCH(int,kind);QFETCH(Channel::Failure,failure);OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");QSignalSpy created(&channel,&Channel::sessionCreated);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(channel.createSession(identity,{},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    QVERIFY(router.send(statusFrame(kind==1||kind==4?65535:13,static_cast<wire::SessionState>(kind))));QTRY_COMPARE(channel.state(),Channel::State::Failed);
    QCOMPARE(channel.failure(),failure);QCOMPARE(created.count(),0);QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(!channel.deadlineActive());QVERIFY(!channel.socketActive());
  }
  void reconfigureCannotChangeIdentityOrAcceptWrongId(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(channel.createSession(identity,{{"keep","old"}},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto first=*sentFrame(router.received,hello().size());QVERIFY(router.send(status));QTRY_VERIFY(channel.sessionId().has_value());
    auto destination=identity.destination()->wire;destination[0]^=1;auto changed=i2p::makeClientIdentity(destination,i2p::Bytes(256),identity.signingPrivateKey());QVERIFY(changed);
    const auto refused=channel.reconfigureSession(*changed,{{"keep","new"}},1761739200123);QVERIFY(!refused);QCOMPARE(refused.error().code,i2p::ErrorCode::KeyMismatch);
    QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(channel.sessionOptions()==wire::SessionOptions({{"keep","old"}}));
    QSignalSpy updated(&channel,&Channel::sessionUpdated);QVERIFY(channel.reconfigureSession(identity,{{"keep","new"}},1761739200123));
    const auto offset=hello().size()+5+static_cast<qsizetype>(first.payload.size());QTRY_VERIFY(sentFrame(router.received,offset).has_value());
    QVERIFY(router.send(statusFrame(0xabce,wire::SessionState::Updated)));QTRY_COMPARE(channel.state(),Channel::State::Failed);QCOMPARE(channel.failure(),Channel::Failure::InvalidSessionId);
    QCOMPARE(updated.count(),0);QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(!channel.socketActive());
  }
  void pendingCreateDeadlineClosesWithoutAdmissionOrRetry(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");auto options=router.options();options.deadlineMilliseconds=1000;
    QVERIFY(channel.begin(options));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(channel.createSession(identity,{},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    QTRY_COMPARE_WITH_TIMEOUT(channel.state(),Channel::State::Failed,3000);QCOMPARE(channel.failure(),Channel::Failure::Deadline);
    QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(!channel.deadlineActive());QVERIFY(!channel.socketActive());QCOMPARE(router.accepted,1);
  }
  void invalidSigningInputDoesNotReplaceTheDateOrWriteARequest(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");i2p::ClientIdentity absent;
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(!channel.createSession(absent,{},1761739200123));QVERIFY(!channel.createSession(identity,{},0));
    QVERIFY(!channel.createSession(identity,{},1761739200123+10ULL*24*60*60*1000));
    QVERIFY(!channel.createSession(identity,{{"a","1"},{"a","2"}},1761739200123));
    QVERIFY(!channel.createSession(identity,{{"i2cp.username","synthetic-only"}},1761739200123));
    const auto password=channel.createSession(identity,{{"i2cp.password",""}},1761739200123);QVERIFY(!password);QCOMPARE(password.error().code,i2p::ErrorCode::Unsupported);
    QVERIFY(!channel.createSession(identity,{{"i2cp.username",""},{"i2cp.password",""}},1761739200123));
    QCOMPARE(channel.state(),Channel::State::DateReceived);QVERIFY(!channel.sessionId());QVERIFY(!channel.deadlineActive());
    QVERIFY(channel.createSession(identity,{{"valid","only"}},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto parsed=wire::decodeSessionMessage(*sentFrame(router.received,hello().size()));QVERIFY(parsed);
    const auto& accepted=std::get<wire::CreateSession>(*parsed).config;QVERIFY(accepted.options==wire::SessionOptions({{"valid","only"}}));QVERIFY(wire::verifySessionConfigSignature(accepted));
    QVERIFY(router.send(status));QTRY_VERIFY(channel.sessionId().has_value());QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void previouslyReadPartialStatusCannotBecomeANewCreateAcknowledgement(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");QSignalSpy created(&channel,&Channel::sessionCreated);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    // Observe the actual socket readyRead after its earlier-registered private
    // drain handler. This is an I/O barrier, not a fabricated protocol token.
    auto* socket=channel.findChild<QTcpSocket*>();QVERIFY(socket);QSignalSpy read(socket,&QTcpSocket::readyRead);
    QVERIFY(router.send(status.left(4)));QTRY_VERIFY(read.count()>0);
    QVERIFY(!channel.createSession(identity,{},1761739200123));QCOMPARE(channel.state(),Channel::State::DateReceived);
    QVERIFY(router.send(status.mid(4)));QTRY_COMPARE(channel.state(),Channel::State::Failed);QCOMPARE(channel.failure(),Channel::Failure::UnexpectedMessage);
    QCOMPARE(created.count(),0);QVERIFY(!channel.sessionId());QCOMPARE(router.received,hello());
  }
  void unsupportedTlsCannotBecomePlaintextRegistration(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");auto options=router.options();options.tlsRequired=true;
    QVERIFY(!channel.begin(options));QCOMPARE(channel.failure(),Channel::Failure::UnsupportedTransport);QVERIFY(!channel.socketActive());QVERIFY(!channel.deadlineActive());QCOMPARE(router.accepted,0);
    options.tlsRequired=false;QVERIFY(channel.begin(options));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    const auto refused=channel.createSession(identity,{{"i2cp.SSL","TrUe"}},1761739200123);QVERIFY(!refused);QCOMPARE(refused.error().code,i2p::ErrorCode::Unsupported);
    QCOMPARE(channel.state(),Channel::State::DateReceived);QVERIFY(!channel.sessionId());
    QVERIFY(channel.createSession(identity,{{"i2cp.SSL","false"},{"valid","plain-loopback"}},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto frame=wire::decodeSessionMessage(*sentFrame(router.received,hello().size()));QVERIFY(frame);QVERIFY(std::get<wire::CreateSession>(*frame).config.options==wire::SessionOptions({{"valid","plain-loopback"}}));
    QVERIFY(router.send(status));QTRY_VERIFY(channel.sessionId().has_value());
  }
  void cancelBeforeQueuedOldStatusThenNewRegistrationCannotReuseIt(){
    OwnedRouter first,second;QVERIFY(first.listen());QVERIFY(second.listen());Channel channel;auto identity=signer("ed25519");QSignalSpy created(&channel,&Channel::sessionCreated);
    QVERIFY(channel.begin(first.options()));QTRY_COMPARE(first.received,hello());QVERIFY(first.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(channel.createSession(identity,{},1761739200123));QTRY_VERIFY(sentFrame(first.received,hello().size()).has_value());
    QVERIFY(first.send(status));if(first.peer->bytesToWrite()>0){QVERIFY(first.peer->flush());}
    channel.close();QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QCOMPARE(created.count(),0);
    QVERIFY(channel.begin(second.options()));QTRY_COMPARE(second.received,hello());QVERIFY(second.send(legacy));QTRY_VERIFY(channel.routerDate().has_value());
    QCOMPARE(created.count(),0);QVERIFY(channel.createSession(identity,{},0x0102030405060708ULL));QTRY_VERIFY(sentFrame(second.received,hello().size()).has_value());
    QVERIFY(second.send(statusFrame(0,wire::SessionState::Created)));QTRY_COMPARE(created.count(),1);QVERIFY(channel.sessionId()==std::optional<std::uint16_t>(0));
    channel.close();QVERIFY(!channel.sessionId());QVERIFY(channel.sessionOptions().empty());QVERIFY(!channel.socketActive());
  }
  void nestedRealReconfigureCannotPublishTheSupersededCreatedNotification(){
    OwnedRouter router;QVERIFY(router.listen());Channel channel;auto identity=signer("ed25519");QSignalSpy created(&channel,&Channel::sessionCreated),updated(&channel,&Channel::sessionUpdated);
    QVERIFY(channel.begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel.routerDate().has_value());
    QVERIFY(channel.createSession(identity,{{"keep","old"}},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto first=*sentFrame(router.received,hello().size());const auto offset=hello().size()+5+static_cast<qsizetype>(first.payload.size());
    bool nested=false,requestAccepted=false,replyValidated=false,replySent=false,finished=false;
    QObject::connect(&channel,&Channel::stateChanged,&channel,[&](Channel::State next){
      if(next!=Channel::State::SessionCreated||nested){return;}nested=true;
      requestAccepted=channel.reconfigureSession(identity,{{"added","new"}},1761739200123).has_value();if(!requestAccepted){return;}
      QEventLoop loop;QTimer guard;guard.setSingleShot(true);
      QObject::connect(&guard,&QTimer::timeout,&loop,&QEventLoop::quit);
      QObject::connect(&channel,&Channel::sessionUpdated,&loop,[&]{finished=true;loop.quit();});
      auto reply=[&]{
        if(replySent){return;}const auto frame=sentFrame(router.received,offset);if(!frame){return;}
        auto message=wire::decodeSessionMessage(*frame);if(!message||!std::holds_alternative<wire::ReconfigureSession>(*message)){loop.quit();return;}
        const auto& update=std::get<wire::ReconfigureSession>(*message);
        replyValidated=update.sessionId==0xabcd&&wire::verifySessionConfigSignature(update.config).has_value()&&update.config.options==wire::SessionOptions({{"added","new"}});
        if(!replyValidated){loop.quit();return;}replySent=router.send(statusFrame(0xabcd,wire::SessionState::Updated));if(!replySent){loop.quit();}
      };
      QObject::connect(router.peer,&QTcpSocket::readyRead,&loop,reply);reply();guard.start(2000);loop.exec();
    });
    QVERIFY(router.send(status));QTRY_VERIFY(nested);QVERIFY(requestAccepted);QVERIFY(replyValidated);QVERIFY(replySent);QVERIFY(finished);
    QCOMPARE(created.count(),0);QCOMPARE(updated.count(),1);QCOMPARE(channel.state(),Channel::State::SessionCreated);
    QVERIFY(channel.sessionOptions()==wire::SessionOptions({{"added","new"},{"keep","old"}}));
  }
  void registrationNotificationMayDeleteController_data(){
    QTest::addColumn<bool>("onUpdate");QTest::newRow("created")<<false;QTest::newRow("updated")<<true;
  }
  void registrationNotificationMayDeleteController(){
    QFETCH(bool,onUpdate);OwnedRouter router;QVERIFY(router.listen());auto identity=signer("ed25519");QPointer<Channel> channel=new Channel(&router.server);
    QVERIFY(channel->begin(router.options()));QTRY_COMPARE(router.received,hello());QVERIFY(router.send(date));QTRY_VERIFY(channel->routerDate().has_value());
    QVERIFY(channel->createSession(identity,{},1761739200123));QTRY_VERIFY(sentFrame(router.received,hello().size()).has_value());
    const auto first=*sentFrame(router.received,hello().size());
    if(!onUpdate){QObject::connect(channel,&Channel::sessionCreated,&router.server,[&]{delete channel.data();});}
    QVERIFY(router.send(status));
    if(onUpdate){
      QTRY_VERIFY(channel->sessionId().has_value());QObject::connect(channel,&Channel::sessionUpdated,&router.server,[&]{delete channel.data();});
      QVERIFY(channel->reconfigureSession(identity,{{"new","value"}},1761739200123));
      const auto offset=hello().size()+5+static_cast<qsizetype>(first.payload.size());QTRY_VERIFY(sentFrame(router.received,offset).has_value());
      QVERIFY(router.send(statusFrame(0xabcd,wire::SessionState::Updated)));
    }
    QTRY_VERIFY(channel.isNull());QTRY_COMPARE(router.peer->state(),QAbstractSocket::UnconnectedState);
  }
};
QTEST_GUILESS_MAIN(InitialChannelTests)
#include "i2cp_initial_channel_tests.moc"
