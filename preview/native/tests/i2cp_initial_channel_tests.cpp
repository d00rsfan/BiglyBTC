// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <optional>

namespace wire=biglybtc::services::i2p::i2cp;
using Channel=wire::InitialChannel;
Q_DECLARE_METATYPE(Channel::Failure)

namespace {
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
};
QTEST_GUILESS_MAIN(InitialChannelTests)
#include "i2cp_initial_channel_tests.moc"
