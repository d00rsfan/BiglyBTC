// SPDX-License-Identifier: GPL-2.0-or-later
#include "ownership.hpp"
#include <QHostAddress>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QTcpServer>
#include <QThread>
#include <QtTest>
using biglybtc::services::tor::OwnershipHandshake;
namespace {
QByteArray nonce(){QByteArray b(32,'\0');for(int i=0;i<32;++i){b[i]=static_cast<char>(i);}return b;}
const QByteArray challenge="250 AUTHCHALLENGE SERVERHASH=9AC6D0EF401195DCFCBDCBC6E2876DA2DDB4319D4D92242C4E402C450BA848C8 SERVERNONCE=1F1E1D1C1B1A191817161514131211100F0E0D0C0B0A09080706050403020100";
const QByteArray proof="AUTHENTICATE F40A1FFB4D7754B5206A40D9DB648ED970DC28CBF45C2AA35C2A9899AAF5B882";
const QList<QByteArray> expectedCommands={"AUTHCHALLENGE SAFECOOKIE "+nonce().toHex().toUpper(),proof,"TAKEOWNERSHIP","RESETCONF __OwningControllerProcess","GETINFO version"};
// Every instance creates a new listener on numeric127.0.0.1:0. It does not
// emulate a process, read an identity/cookie file, or contact any existing port.
class Peer final : public QObject {
public:
  enum class Mode { Good,Fragmented,EmptyInfo,LastInfo,AckSuffix,RejectTake,RejectReset,RejectInfo,
      HangTake,HangReset,HangInfo,BadProof,EofTake,EofReset,EofInfo,BadFrame,ManyLines,ManyBytes,LongLine,ShortStatus,Delayed,HangChallenge };
  explicit Peer(Mode requested=Mode::Good):mode(requested),server(this){
    connect(&server,&QTcpServer::newConnection,this,[this]{
      while(server.hasPendingConnections()){
        auto* client=server.nextPendingConnection();client->setReadBufferSize(4096);clients.append(client);
        connect(client,&QTcpSocket::readyRead,client,[this,client]{
          while(client->canReadLine()){
            auto command=client->readLine();command.chop(2);commands.append(command);const auto index=commands.size()-1;
            if(index<0 || index>=expectedCommands.size() || command!=expectedCommands[index]){unexpected=true;client->abort();return;}
            if((index==0 && mode==Mode::HangChallenge)||(index==2 && mode==Mode::HangTake)||(index==3 && mode==Mode::HangReset)||(index==4 && mode==Mode::HangInfo)){continue;}
            if((index==2 && mode==Mode::EofTake)||(index==3 && mode==Mode::EofReset)||(index==4 && mode==Mode::EofInfo)){client->disconnectFromHost();continue;}
            QByteArray reply;
            if(index==0){reply=challenge;if(mode==Mode::BadProof){reply[29]=reply[29]=='0'?'1':'0';}reply+="\r\n";}
            else if(index==1){reply="250 OK\r\n";}
            else if(index==2){reply=mode==Mode::RejectTake?"510 Unrecognized command\r\n":mode==Mode::AckSuffix?"  250 OK extra\t\r\n":"250 OK\r\n";}
            else if(index==3){reply=mode==Mode::RejectReset?"552 Invalid configuration\r\n":mode==Mode::AckSuffix?"250 OK suffix\r\n":"250 OK\r\n";}
            else{
              if(mode==Mode::RejectInfo){reply="552 Unrecognized key\r\n";}
              else if(mode==Mode::EmptyInfo){reply="250 OK\r\n";}
              else if(mode==Mode::LastInfo){reply="250-version=first\r\n250 version=last raw value\r\n250 OK\r\n";}
              else if(mode==Mode::BadFrame){reply="250-version=bad\rinside\r\n";}
              else if(mode==Mode::ManyLines){reply=QByteArray("250-version=x\r\n").repeated(33)+"250 OK\r\n";}
              else if(mode==Mode::ManyBytes){reply=("250-version="+QByteArray(1000,'x')+"\r\n").repeated(5)+"250 OK\r\n";}
              else if(mode==Mode::LongLine){reply="250-version="+QByteArray(2000,'x')+"\r\n250 OK\r\n";}
              else if(mode==Mode::ShortStatus){reply="250\r\n250 OK\r\n";}
              else{reply="250-version=fixture raw 0.1\r\n250 OK\r\n";}
            }
            if(mode==Mode::Fragmented){const auto split=reply.size()/2;client->write(reply.first(split));QTimer::singleShot(5,client,[client,reply,split]{client->write(reply.sliced(split));});}
            else if(mode==Mode::Delayed){QTimer::singleShot(30,client,[client,reply]{client->write(reply);});}
            else{client->write(reply);}
          }
        });
      }
    });listening=server.listen(QHostAddress::LocalHost,0);
  }
  quint16 port() const{return server.serverPort();}
  Mode mode;QTcpServer server;QList<QByteArray> commands;QList<QPointer<QTcpSocket>> clients;bool listening=false,unexpected=false;
};
}
class OwnershipTests final : public QObject {
  Q_OBJECT
private slots:
  void setupSequence_data(){
    QTest::addColumn<int>("mode");QTest::addColumn<QByteArray>("version");
    QTest::newRow("whole")<<static_cast<int>(Peer::Mode::Good)<<QByteArray("version=fixture raw 0.1");
    QTest::newRow("fragmented")<<static_cast<int>(Peer::Mode::Fragmented)<<QByteArray("version=fixture raw 0.1");
    QTest::newRow("empty-source-info")<<static_cast<int>(Peer::Mode::EmptyInfo)<<QByteArray{};
    QTest::newRow("last-source-info")<<static_cast<int>(Peer::Mode::LastInfo)<<QByteArray("version=last raw value");
    QTest::newRow("source-prefix-ack")<<static_cast<int>(Peer::Mode::AckSuffix)<<QByteArray("version=fixture raw 0.1");
  }
  void setupSequence(){
    QFETCH(int,mode);QFETCH(QByteArray,version);Peer peer(static_cast<Peer::Mode>(mode));QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed),closed(&setup,&OwnershipHandshake::closed);
    QVERIFY(setup.rawVersionInfo().isEmpty());QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000));QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);QCOMPARE(failed.count(),0);QCOMPARE(peer.commands,expectedCommands);QVERIFY(!peer.unexpected);QCOMPARE(setup.state(),OwnershipHandshake::State::Ready);QCOMPARE(setup.rawVersionInfo(),version);QVERIFY(!setup.deadlineActive());setup.stop();setup.stop();QCOMPARE(closed.count(),1);QCOMPARE(setup.state(),OwnershipHandshake::State::Closed);QCOMPARE(setup.rawVersionInfo(),version);QVERIFY(!setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce()));
  }
  void failureNeverSignalsReady_data(){
    QTest::addColumn<int>("mode");
    for(auto mode:{Peer::Mode::RejectTake,Peer::Mode::RejectReset,Peer::Mode::RejectInfo,Peer::Mode::HangTake,Peer::Mode::HangReset,Peer::Mode::HangInfo,Peer::Mode::BadProof,Peer::Mode::EofTake,Peer::Mode::EofReset,Peer::Mode::EofInfo,Peer::Mode::BadFrame,Peer::Mode::ManyLines,Peer::Mode::ManyBytes,Peer::Mode::LongLine,Peer::Mode::ShortStatus}){QTest::newRow(qPrintable(QString::number(static_cast<int>(mode))))<<static_cast<int>(mode);}
  }
  void failureNeverSignalsReady(){
    QFETCH(int,mode);Peer peer(static_cast<Peer::Mode>(mode));QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed),closed(&setup,&OwnershipHandshake::closed);
    QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),200));QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,3000);QCOMPARE(ready.count(),0);QCOMPARE(closed.count(),0);QCOMPARE(setup.state(),OwnershipHandshake::State::Failed);QVERIFY(!setup.deadlineActive());QVERIFY(setup.rawVersionInfo().isEmpty());QVERIFY(!peer.unexpected);QVERIFY(!failed[0][0].toString().contains("SERVERHASH"));setup.stop();QCOMPARE(failed.count(),1);QCOMPARE(closed.count(),0);
    const auto m=static_cast<Peer::Mode>(mode);const int expected=m==Peer::Mode::BadProof?1:(m==Peer::Mode::RejectTake || m==Peer::Mode::HangTake || m==Peer::Mode::EofTake)?3:(m==Peer::Mode::RejectReset || m==Peer::Mode::HangReset || m==Peer::Mode::EofReset)?4:5;QCOMPARE(peer.commands.size(),expected);
  }
  void cancelFromState_data(){
    QTest::addColumn<int>("state");
    for(auto state:{OwnershipHandshake::State::Authenticating,OwnershipHandshake::State::TakingOwnership,OwnershipHandshake::State::ResettingOwner,OwnershipHandshake::State::GettingInfo,OwnershipHandshake::State::Ready}){QTest::newRow(qPrintable(QString::number(static_cast<int>(state))))<<static_cast<int>(state);}
  }
  void cancelFromState(){
    QFETCH(int,state);Peer peer;QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),closed(&setup,&OwnershipHandshake::closed);
    connect(&setup,&OwnershipHandshake::stateChanged,&setup,[&](OwnershipHandshake::State s){if(static_cast<int>(s)==state){setup.stop();}});QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000));QTRY_COMPARE(closed.count(),1);QCOMPARE(ready.count(),0);QVERIFY(!setup.deadlineActive());QTest::qWait(20);const auto s=static_cast<OwnershipHandshake::State>(state);const int count=s==OwnershipHandshake::State::Authenticating?0:s==OwnershipHandshake::State::TakingOwnership?2:s==OwnershipHandshake::State::ResettingOwner?3:s==OwnershipHandshake::State::GettingInfo?4:5;QCOMPARE(peer.commands.size(),count);
  }
  void destructionFromState_data(){cancelFromState_data();}
  void destructionFromState(){
    QFETCH(int,state);Peer peer;QVERIFY(peer.listening);QPointer<OwnershipHandshake> setup=new OwnershipHandshake;connect(setup,&OwnershipHandshake::stateChanged,&peer,[setup,state](OwnershipHandshake::State s){if(static_cast<int>(s)==state){delete setup.data();}});setup->beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000);QTRY_VERIFY(setup.isNull());QTest::qWait(20);
  }
  void destructionFromReadyAndFailure_data(){QTest::addColumn<bool>("failure");QTest::newRow("ready")<<false;QTest::newRow("failed")<<true;}
  void destructionFromReadyAndFailure(){
    QFETCH(bool,failure);Peer peer(failure?Peer::Mode::RejectTake:Peer::Mode::Good);QVERIFY(peer.listening);QPointer<OwnershipHandshake> setup=new OwnershipHandshake;connect(setup,&OwnershipHandshake::ready,&peer,[setup]{delete setup.data();});connect(setup,&OwnershipHandshake::failed,&peer,[setup]{delete setup.data();});setup->beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000);QTRY_VERIFY(setup.isNull());QTest::qWait(20);
  }
  void destructionEmitsNoPublicSignals_data(){
    QTest::addColumn<bool>("ready");QTest::newRow("ready")<<true;QTest::newRow("pending-take")<<false;
  }
  void destructionEmitsNoPublicSignals(){
    QFETCH(bool,ready);Peer peer(ready?Peer::Mode::Good:Peer::Mode::HangTake);QVERIFY(peer.listening);
    QPointer<OwnershipHandshake> setup=new OwnershipHandshake;const auto cleanup=qScopeGuard([&]{delete setup.data();});
    QSignalSpy states(setup,&OwnershipHandshake::stateChanged),readies(setup,&OwnershipHandshake::ready),failed(setup,&OwnershipHandshake::failed),closed(setup,&OwnershipHandshake::closed);
    QVERIFY(setup->beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000));
    if(ready){QTRY_COMPARE(readies.count(),1);QCOMPARE(setup->state(),OwnershipHandshake::State::Ready);}
    else{QTRY_COMPARE(peer.commands.size(),3);QCOMPARE(setup->state(),OwnershipHandshake::State::TakingOwnership);QVERIFY(setup->deadlineActive());}
    QCOMPARE(failed.count(),0);QCOMPARE(closed.count(),0);const auto stateCount=states.count(),readyCount=readies.count();
    delete setup.data();QVERIFY(setup.isNull());
    QCOMPARE(states.count(),stateCount);QCOMPARE(readies.count(),readyCount);QCOMPARE(failed.count(),0);QCOMPARE(closed.count(),0);
    QTest::qWait(20);QCOMPARE(states.count(),stateCount);QCOMPARE(readies.count(),readyCount);QCOMPARE(failed.count(),0);QCOMPARE(closed.count(),0);QVERIFY(!peer.unexpected);
  }
  void invalidParametersOpenNoConnection(){
    Peer peer;QVERIFY(peer.listening);for(int i=0;i<5;++i){OwnershipHandshake setup;QSignalSpy failed(&setup,&OwnershipHandshake::failed);QVERIFY(!setup.beginOwnedPeer(i==0?0:peer.port(),QByteArray(i==1?31:32,'\0'),i==2?QByteArray(31,'x'):nonce(),i==3?0:i==4?30001:1000));QCOMPARE(failed.count(),1);QVERIFY(!setup.deadlineActive());}QTest::qWait(20);QVERIFY(peer.clients.empty());
  }
  void oneDeadlineIncludesAuthenticationAndSetup(){
    Peer peer(Peer::Mode::Delayed);QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed);QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),110));QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,3000);QCOMPARE(ready.count(),0);QVERIFY(!setup.deadlineActive());QVERIFY(peer.commands.size()<=4);
  }
  void lateOwnershipAckDoesNotBeatDeadline(){
    Peer peer(Peer::Mode::HangTake);QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed);QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),100));QTRY_COMPARE(peer.commands.size(),3);QThread::msleep(110);QVERIFY(peer.clients[0]);peer.clients[0]->write("250 OK\r\n");QTRY_COMPARE(failed.count(),1);QCOMPARE(ready.count(),0);QCOMPARE(peer.commands.size(),3);QVERIFY(!setup.deadlineActive());
  }
  void lateAuthenticationCannotWrite_data(){
    QTest::addColumn<bool>("challenge");QTest::newRow("late-connection")<<false;QTest::newRow("late-challenge")<<true;
  }
  void lateAuthenticationCannotWrite(){
    QFETCH(bool,challenge);Peer peer(challenge?Peer::Mode::HangChallenge:Peer::Mode::Good);QVERIFY(peer.listening);OwnershipHandshake setup;
    QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed);
    auto* channel=setup.findChild<biglybtc::services::tor::ControlChannel*>();QVERIFY(channel);
    auto* totalTimer=setup.findChild<QChronoTimer*>(QString(),Qt::FindDirectChildrenOnly);auto* authTimer=channel->findChild<QTimer*>(QString(),Qt::FindDirectChildrenOnly);QVERIFY(totalTimer && authTimer);
    // Suppress timer notifications to model late delivery and require the
    // absolute-expiry checks on socket transitions themselves to stop writes.
    const QSignalBlocker totalBlocked(totalTimer),authBlocked(authTimer);
    if(!challenge){connect(channel,&biglybtc::services::tor::ControlChannel::stateChanged,&setup,[](auto state){if(state==biglybtc::services::tor::ControlChannel::State::Connecting){QThread::msleep(110);}});}
    QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),100));
    if(challenge){QTRY_COMPARE(peer.commands.size(),1);QThread::msleep(110);QVERIFY(peer.clients[0]);peer.clients[0]->write(::challenge+"\r\n");peer.clients[0]->flush();}
    QTRY_COMPARE(failed.count(),1);QCOMPARE(ready.count(),0);QVERIFY(!setup.deadlineActive());QCOMPARE(peer.commands.size(),challenge?1:0);QVERIFY(!peer.unexpected);
  }
  void stopCancelsDeadline(){
    Peer peer(Peer::Mode::HangInfo);QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy failed(&setup,&OwnershipHandshake::failed),closed(&setup,&OwnershipHandshake::closed);QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),300));QTRY_COMPARE(peer.commands.size(),5);setup.stop();QTest::qWait(350);QCOMPARE(closed.count(),1);QCOMPARE(failed.count(),0);QVERIFY(!setup.deadlineActive());
  }
  void eofAfterReadyFailsConnection(){
    Peer peer;QVERIFY(peer.listening);OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed);QVERIFY(setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000));QTRY_COMPARE(ready.count(),1);peer.clients[0]->disconnectFromHost();QTRY_COMPARE(failed.count(),1);QCOMPARE(setup.state(),OwnershipHandshake::State::Failed);QVERIFY(!setup.deadlineActive());QCOMPARE(setup.rawVersionInfo(),QByteArray("version=fixture raw 0.1"));
  }
  void wrongThreadOpensNoConnection(){
    Peer peer;QVERIFY(peer.listening);QThread worker;worker.start();OwnershipHandshake setup;setup.moveToThread(&worker);auto* main=QThread::currentThread();const auto cleanup=qScopeGuard([&]{QMetaObject::invokeMethod(&setup,[&]{setup.moveToThread(main);},Qt::BlockingQueuedConnection);worker.quit();if(!worker.wait(3000)){qFatal("owned control worker stop");}});QVERIFY(!setup.beginOwnedPeer(peer.port(),QByteArray(32,'\0'),nonce(),1000));setup.stop();QTest::qWait(20);QVERIFY(peer.clients.empty());
  }
};
QTEST_GUILESS_MAIN(OwnershipTests)
#include "ownership_tests.moc"
