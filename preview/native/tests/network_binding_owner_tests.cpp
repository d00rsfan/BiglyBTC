#include "network_binding.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <ifaddrs.h>
#include <libtorrent/torrent_status.hpp>
#include <chrono>
#include <cerrno>
#include <memory>
namespace {
using namespace biglybtc;
namespace lt=libtorrent;
using namespace std::chrono_literals;
thread_local bool failEnumeration=false;
thread_local unsigned failedEnumerations=0;
struct FailEnumeration {
  FailEnumeration(){failedEnumerations=0;failEnumeration=true;}
  ~FailEnumeration(){failEnumeration=false;}
};
engine::NetworkPolicy localPolicy(bool ipv6,bool utp){
  engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.ipv6=ipv6;
  p.bindAddresses={ipv6?"lo[v6]":"lo[v4]"};p.enforceBind=true;p.checkBindOnStart=false;
  p.incomingTcp=p.outgoingTcp=!utp;p.incomingUtp=p.outgoingUtp=utp;
  return p; // Explicit loopback,port0; all discovery/proxy/public sources absent.
}
void tracker(test::OwnedEngineFixture& fixture,const QString& url){
  auto doc=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(bool(doc),"generated state");
  const auto text=url.toUtf8();test::require(doc->replaceValue("announce",std::to_string(text.size())+":"+text.toStdString()).has_value(),"owned tracker source");
  test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));
}
QByteArray compactPeer(bool ipv6,unsigned short port){
  QByteArray bytes(ipv6?16:4,'\0');if(ipv6){bytes[15]='\1';}else{bytes[0]='\x7f';bytes[3]='\1';}
  bytes+=static_cast<char>(port>>8);bytes+=static_cast<char>(port);return bytes;
}
}
extern "C" int __real_getifaddrs(ifaddrs**);
extern "C" int __wrap_getifaddrs(ifaddrs** out){
  if(failEnumeration){++failedEnumerations;*out=nullptr;errno=EIO;return -1;}return __real_getifaddrs(out);
}
class NetworkBindingOwnerTests final:public QObject{
  Q_OBJECT
private slots:
  void actualLoopbackRoutes_data(){
    QTest::addColumn<bool>("ipv6");QTest::addColumn<bool>("utp");
    QTest::newRow("tcp-v4")<<false<<false;QTest::newRow("utp-v4")<<false<<true;
    QTest::newRow("tcp-v6")<<true<<false;QTest::newRow("utp-v6")<<true<<true;
  }
  void actualLoopbackRoutes(){
    QFETCH(bool,ipv6);QFETCH(bool,utp);
    test::OwnedEngineFixture fixture(false);const auto address=ipv6?QHostAddress(QHostAddress::LocalHostIPv6):QHostAddress(QHostAddress::LocalHost);
    auto p=localPolicy(ipv6,utp);auto resolved=engine::prepareNetworkBinding(p);QVERIFY2(resolved,"owned loopback family must exist; absence is a failure, never skipped");
    auto pack=engine::makeNetworkSettings(resolved->effective);QVERIFY(pack);
    auto seeder=std::make_unique<lt::session>(*pack);lt::error_code error;
    const auto raw=test::read(fixture.stateFile).toStdString();auto metadata=std::make_shared<lt::torrent_info>(raw.data(),static_cast<int>(raw.size()),error);QVERIFY(!error);
    metadata->clear_trackers();lt::add_torrent_params seed;seed.ti=metadata;seed.save_path=(fixture.root.path()+"/seed").toStdString();
    seed.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);seed.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_pex|lt::torrent_flags::disable_lsd;
    const auto handle=seeder->add_torrent(seed,error);QVERIFY(!error&&handle.is_valid());
    QTRY_VERIFY_WITH_TIMEOUT(seeder->listen_port()!=0&&handle.status().is_seeding,30000);
    QTcpServer server;QVERIFY(server.listen(address,0));unsigned requests=0;bool wrongRoute=false;
    connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){
      auto* socket=server.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
      wrongRoute=wrongRoute||socket->peerAddress()!=address||socket->localAddress()!=address;
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{
        *pending+=socket->readAll();if(pending->size()>16384){wrongRoute=true;socket->abort();return;}
        if(!pending->contains("\r\n\r\n")){return;}
        if(!pending->startsWith("GET /announce?")||!pending->contains("info_hash=")){wrongRoute=true;socket->abort();return;}
        ++requests;const auto peer=compactPeer(ipv6,seeder->listen_port());const QByteArray body="d8:intervali3600e"+QByteArray(ipv6?"6:peers618:":"5:peers6:")+peer+'e';
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();pending->clear();
      });
    }});
    tracker(fixture,QStringLiteral("http://%1:%2/announce").arg(ipv6?QStringLiteral("[::1]"):QStringLiteral("127.0.0.1")).arg(server.serverPort()));
    p.httpTrackers=true;auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({190,191,static_cast<std::uint64_t>(ipv6?utp?4:3:utp?2:1)});QVERIFY2(owner,"source-bound runtime required; no unqualified skip");
    QVERIFY((*owner)->configureNetwork(p));QVERIFY((*owner)->startIdle(*lease));const auto actual=(*owner)->liveSettings();QVERIFY(actual);
    const auto endpoint=ipv6?"::1":"127.0.0.1";QCOMPARE(actual->outgoingInterfaces,std::string(endpoint));QVERIFY(!actual->dht&&!actual->localDiscovery&&!actual->upnp&&!actual->natpmp);
    QVERIFY((*owner)->startManaged(*lease,{*fixture.hash,0,true,false}));
    const auto completed=[&]{static_cast<void>((*owner)->poll());const auto rows=(*owner)->managedDownloads();return rows.size()==1&&rows[0].complete&&rows[0].received>0;};
    QTRY_VERIFY_WITH_TIMEOUT(completed(),30000);QVERIFY(requests>0);QVERIFY(!wrongRoute);
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QVERIFY(fixture.intact());
  }
  void missingSelectedInterfacePreventsStartup(){
    test::OwnedEngineFixture fixture(false);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({195,196,197});QVERIFY(owner);auto p=localPolicy(false,false);p.bindAddresses={"lo[4095]"};
    QVERIFY((*owner)->configureNetwork(p));QVERIFY(!(*owner)->startIdle(*lease));QVERIFY(!(*owner)->tcpListenPort());QVERIFY(!(*owner)->managedAvailable());
    const auto result=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(result.sessionJoined);QVERIFY(!result.retirement.peerCounters);QVERIFY(fixture.intact(false));
  }
  void failedInterfaceObservationJoinsWithoutInventingFinalSample(){
    test::OwnedEngineFixture fixture(false);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto owner=engine::PersistentSession::create({201,202,203});QVERIFY(owner);QVERIFY((*owner)->configureNetwork(localPolicy(false,false)));QVERIFY((*owner)->startIdle(*lease));
    const auto sampled=[&]{static_cast<void>((*owner)->poll());const auto value=(*owner)->latest();return value&&value->observed.has_value();};QTRY_VERIFY_WITH_TIMEOUT(sampled(),10000);
    const auto before=(*owner)->latest();QVERIFY(before&&before->observed);std::expected<void,engine::AccountingError> failed;
    {FailEnumeration fault;failed=(*owner)->poll();}
    QVERIFY(!failed);QCOMPARE(failedEnumerations,1U);const auto after=(*owner)->latest();QVERIFY(after&&after->observed);QCOMPARE(after->state,engine::AccountingState::Uncertified);
    QCOMPARE(*after->observed,*before->observed);QVERIFY(after->sequence>before->sequence);QVERIFY(!(*owner)->managedAvailable());QVERIFY(!(*owner)->tcpListenPort());
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined);QVERIFY(!closed.retirement.peerCounters);QVERIFY(fixture.intact(false));
  }
};
QTEST_GUILESS_MAIN(NetworkBindingOwnerTests)
#include "network_binding_owner_tests.moc"
