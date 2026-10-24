#include "persistent_session.hpp"
#include "network_policy.hpp"
#include "owned_engine_fixture.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>
#include <chrono>
#include <memory>
namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
std::uint32_t number(const QByteArray& bytes, qsizetype at) {
  std::uint32_t value=0;
  for(int i=0;i<4;++i) { value=(value<<8)|static_cast<unsigned char>(bytes[at+i]); }
  return value;
}
void append(QByteArray& bytes,std::uint32_t value) {
  for(int shift=24;shift>=0;shift-=8) { bytes.append(static_cast<char>(value>>shift)); }
}
QByteArray peer(unsigned short port) {
  QByteArray bytes("\x7f\0\0\x01",4);
  bytes.append(static_cast<char>(port>>8));bytes.append(static_cast<char>(port));return bytes;
}
void tracker(test::OwnedEngineFixture& fixture,const QString& url) {
  auto doc=core::Document::parse(test::read(fixture.stateFile).toStdString());
  test::require(doc.has_value(),"Invalid tracker fixture state");
  const auto value=url.toUtf8();
  test::require(doc->replaceValue("announce",std::to_string(value.size())+":"+value.toStdString()).has_value(),"Cannot set owned tracker");
  test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));
}
engine::NetworkPolicy policy(bool udp) {
  engine::NetworkPolicy value;
  value.mode=engine::NetworkMode::Normal;value.httpTrackers=!udp;value.udpTrackers=udp;
  return value; // Numeric loopback bind/port0, no DHT/PEX/LSD/router/web seeds.
}
}
class ManagedTrackerTests final:public QObject {
  Q_OBJECT
private slots:
  void httpTrackerActuallyDiscoversOwnedPeer() {
    test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));int requests=0;bool invalid=false;
    connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()) {
      auto* socket=server.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{
        *pending+=socket->readAll();if(pending->size()>16384) { invalid=true;socket->abort();return; }
        if(!pending->contains("\r\n\r\n")) { return; }
        if(!pending->startsWith("GET /announce?")||!pending->contains("info_hash=")) { invalid=true;socket->abort();return; }
        ++requests;
        const QByteArray body="d8:intervali3600e5:peers6:"+peer(fixture.port())+'e';
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        socket->disconnectFromHost();pending->clear();
      });
    }});
    tracker(fixture,QStringLiteral("http://127.0.0.1:%1/announce").arg(server.serverPort()));
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({71,72,73});QVERIFY(owner);
    QVERIFY((*owner)->configureNetwork(policy(false)));QVERIFY((*owner)->startIdle(*lease));
    QVERIFY((*owner)->startManaged(*lease,{*fixture.hash,0,true,false}));
    const auto completed=[&]{static_cast<void>((*owner)->poll());const auto states=(*owner)->managedDownloads();return states.size()==1&&states[0].complete;};
    QTRY_VERIFY_WITH_TIMEOUT(completed(),30000);QVERIFY(requests>0);QVERIFY(!invalid);
    const auto facts=(*owner)->trackerStatistics();QVERIFY(facts);QCOMPARE(facts->state,core::TrackerReadState::Ready);
    QVERIFY(!facts->rows.empty());
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined);QVERIFY(closed.retirement.storageReleased);QVERIFY(fixture.intact());
  }
  void udpTrackerActuallyDiscoversOwnedPeer() {
    test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    QUdpSocket server;QVERIFY(server.bind(QHostAddress::LocalHost,0));int connects=0,announces=0;bool invalid=false;
    connect(&server,&QUdpSocket::readyRead,&server,[&]{while(server.hasPendingDatagrams()) {
      QByteArray request;request.resize(server.pendingDatagramSize());QHostAddress sender;quint16 port=0;
      if(server.readDatagram(request.data(),request.size(),&sender,&port)!=request.size()||request.size()<16) { invalid=true;continue; }
      const auto action=number(request,8), transaction=number(request,12);QByteArray reply;
      if(action==0&&request.size()==16) {
        ++connects;append(reply,0);append(reply,transaction);append(reply,0x12345678);append(reply,0xabcdef01);
      } else if(action==1&&request.size()>=98) {
        ++announces;append(reply,1);append(reply,transaction);append(reply,3600);append(reply,0);append(reply,1);reply+=peer(fixture.port());
      } else { invalid=true;continue; }
      server.writeDatagram(reply,sender,port);
    }});
    tracker(fixture,QStringLiteral("udp://127.0.0.1:%1/announce").arg(server.localPort()));
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({81,82,83});QVERIFY(owner);
    QVERIFY((*owner)->configureNetwork(policy(true)));QVERIFY((*owner)->startIdle(*lease));
    QVERIFY((*owner)->startManaged(*lease,{*fixture.hash,0,true,false}));
    const auto completed=[&]{static_cast<void>((*owner)->poll());const auto states=(*owner)->managedDownloads();return states.size()==1&&states[0].complete;};
    QTRY_VERIFY_WITH_TIMEOUT(completed(),30000);QVERIFY(connects>0&&announces>0);QVERIFY(!invalid);
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined);QVERIFY(closed.retirement.storageReleased);QVERIFY(fixture.intact());
  }
};
QTEST_GUILESS_MAIN(ManagedTrackerTests)
#include "managed_tracker_tests.moc"
