// SPDX-License-Identifier: GPL-2.0-or-later
// Actual normal Host, generated mixed public/private library, owned wire endpoints.
#include "ordinary_daily_fixture.hpp"
#include <QApplication>
#include <QUrl>
#include <libtorrent/bdecode.hpp>

namespace {
using namespace biglybtc;
using namespace biglybtc::test::daily;
const QByteArray PathKey("a9410b78c3d52e6f8047192abcde3056");
const QByteArray QueryKey("b619e2f0784a35cd90ef1267ab8435de");
lt::entry entry(const QString& path) {
  const auto bytes = test::read(path);
  lt::bdecode_node node; lt::error_code error;
  lt::bdecode(bytes.constData(), bytes.constData() + bytes.size(), node, error);
  test::require(!error, "Generated bencode must decode");
  return lt::entry(node);
}
QByteArray hashBytes(const Profile& p) { return {reinterpret_cast<const char*>(p.hash.bytes().data()),20}; }
void makePrivate(Profile& p,bool privateFlag) {
  auto torrent=entry(p.state); torrent["info"]["private"]=privateFlag?1:0;
  const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(torrent["info"])),QCryptographicHash::Sha1);
  const auto parsed=core::TorrentHash::parse(std::string_view(digest.constData(),20));
  test::require(bool(parsed),"Generated private identity");
  test::require(QFile::remove(p.state)&&QFile::remove(p.archive),"Replace only generated old private identity");
  p.hash=*parsed; p.state=p.path+"/active/"+QString::fromStdString(p.hash.hex())+".dat";
  p.archive=p.path+"/archive/"+QString::fromStdString(p.hash.hex())+".torrent";
  const auto bytes=QByteArray::fromStdString(test::encoded(torrent)); test::write(p.state,bytes);test::write(p.archive,bytes);
  auto inventory=entry(p.path+"/downloads.config");auto& row=inventory["downloads"].list().front();
  row["torrent_hash"]=digest.toStdString();row["torrent"]=p.archive.toStdString();
  test::write(p.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
}

class HttpTracker final {
  QTcpServer server_; QByteArray expectedHash_; bool credentialed_;
public:
  QList<QByteArray> requests; int announces=0; QString failure;
  HttpTracker(QHostAddress address,bool credentialed):credentialed_(credentialed) {
    test::require(server_.listen(address,0),"Owned HTTP tracker bind");
    QObject::connect(&server_,&QTcpServer::newConnection,&server_,[this]{
      while(server_.hasPendingConnections()) {
        auto* socket=server_.nextPendingConnection();auto pending=std::make_shared<QByteArray>();
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,pending]{
          *pending+=socket->readAll();if(pending->size()>32768){failure="HTTP request limit";socket->abort();return;}
          if(!pending->contains("\r\n\r\n")){return;}
          requests.push_back(*pending);const auto line=pending->left(pending->indexOf("\r\n"));
          const auto target=line.mid(4,line.indexOf(' ',4)-4);const auto query=target.mid(target.indexOf('?')+1);
          QByteArray observed;for(const auto& part:query.split('&')){if(part.startsWith("info_hash=")){observed=QByteArray::fromPercentEncoding(part.mid(10));}}
          if(observed!=expectedHash_){failure="Tracker received another torrent identity";}
          const bool scrape=target.contains("/scrape");
          if(credentialed_&&(!target.contains(PathKey)||!query.contains(QueryKey))){failure="Private credentials absent from intended tracker";}
          QByteArray body;
          if(scrape){lt::entry e(lt::entry::dictionary_type{});auto& f=e["files"][expectedHash_.toStdString()];f["complete"]=1;f["downloaded"]=0;f["incomplete"]=0;body=QByteArray::fromStdString(test::encoded(e));}
          else{++announces;body="d8:intervali3600e8:completei1e10:incompletei0e5:peers0:e";}
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
          socket->disconnectFromHost();pending->clear();
        });
      }
    });
  }
  void expect(const Profile& p){expectedHash_=hashBytes(p);}
  QString url()const {
    const auto host=server_.serverAddress().protocol()==QAbstractSocket::IPv6Protocol?QStringLiteral("[::1]"):QStringLiteral("127.0.0.1");
    const auto suffix=credentialed_?"/announce/"+PathKey+"?passkey="+QueryKey:QByteArray("/announce");
    return QStringLiteral("http://%1:%2%3").arg(host).arg(server_.serverPort()).arg(QString::fromLatin1(suffix));
  }
};
class UdpTracker final {
  QUdpSocket socket_; QByteArray expectedHash_;
public:
  QList<QByteArray> requests; int announces=0; QString failure;
  explicit UdpTracker(QHostAddress address) {
    test::require(socket_.bind(address,0,QUdpSocket::DontShareAddress),"Owned UDP tracker bind");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{while(socket_.hasPendingDatagrams()){
      const auto packet=socket_.receiveDatagram(65536);const auto request=packet.data();requests.push_back(request);
      if(request.size()<16){failure="Short UDP tracker frame";continue;}const auto action=number(request,8),transaction=number(request,12);QByteArray reply;
      if(action==0){append(reply,0);append(reply,transaction);reply+=QByteArray::fromHex("1122334455667788");}
      else if(action==1&&request.size()>=98){if(request.mid(16,20)!=expectedHash_){failure="Public UDP received private/unknown hash";}++announces;append(reply,1);append(reply,transaction);append(reply,3600);append(reply,0);append(reply,1);}
      else if(action==2&&request.size()==36){if(request.mid(16,20)!=expectedHash_){failure="Public scrape received private/unknown hash";}append(reply,2);append(reply,transaction);append(reply,1);append(reply,0);append(reply,0);}
      else{failure="Unexpected UDP tracker frame";continue;}
      if(socket_.writeDatagram(reply,packet.senderAddress(),packet.senderPort())!=reply.size()){failure="UDP tracker reply failed";}
    }});
  }
  void expect(const Profile& p){expectedHash_=hashBytes(p);}
  QString url()const {return QStringLiteral("udp://%1:%2/announce").arg(socket_.localAddress().protocol()==QAbstractSocket::IPv6Protocol?"[::1]":"127.0.0.1").arg(socket_.localPort());}
};
// Transparent owned relay captures complete actual client KRPC frames. Backend
// performs independent transaction/node/hash/token/TCP-port validation.
class KrpcCapture final {
  QUdpSocket socket_; QHostAddress address_,client_; quint16 backend_,clientPort_=0;
public:
  QList<QByteArray> queries; QString failure;
  KrpcCapture(QHostAddress address,quint16 backend):address_(address),backend_(backend) {
    test::require(socket_.bind(address,0,QUdpSocket::DontShareAddress),"Owned KRPC relay bind");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{while(socket_.hasPendingDatagrams()){
      const auto packet=socket_.receiveDatagram(65536);const bool response=packet.senderPort()==backend_;
      if(response){if(clientPort_&&socket_.writeDatagram(packet.data(),client_,clientPort_)!=packet.data().size()){failure="KRPC relay response write failed";}}
      else{if(clientPort_&&clientPort_!=packet.senderPort()){failure="Unexpected second KRPC client";}client_=packet.senderAddress();clientPort_=packet.senderPort();queries.push_back(packet.data());
        if(socket_.writeDatagram(packet.data(),address_,backend_)!=packet.data().size()){failure="KRPC relay request write failed";}}
    }});
  }
  quint16 port()const{return socket_.localPort();}
};
bool noCredential(const QList<QByteArray>& frames) {
  for(const auto& frame:frames){const auto decoded=QByteArray::fromPercentEncoding(frame);
    if(decoded.contains(PathKey)||decoded.contains(QueryKey)||decoded.contains("passkey=")||decoded.toLower().contains("referer:")||decoded.toLower().contains("authorization:")){return false;}
  }return true;
}
}
class TrackerPrivacyTests final:public QObject {
  Q_OBJECT
private slots:
  void mixedLibraryKeepsPrivateCredentialsOffPublicWire_data() {
    QTest::addColumn<bool>("v6");QTest::addColumn<bool>("udp");QTest::addColumn<bool>("privateFlag");
    for(const bool privateFlag:{true,false}){for(const bool v6:{false,true}){for(const bool udp:{false,true}){
      const auto tag=QByteArray(privateFlag?"private-":"public-auth-")+(v6?"IPv6-":"IPv4-")+(udp?"UDP":"HTTP");
      QTest::newRow(tag.constData())<<v6<<udp<<privateFlag;
    }}}
  }
  void mixedLibraryKeepsPrivateCredentialsOffPublicWire() {
    QFETCH(bool,v6);QFETCH(bool,udp);QFETCH(bool,privateFlag);QTemporaryDir root("/tmp/biglybtc-tracker-privacy-XXXXXX");QVERIFY(root.isValid());
    const QHostAddress address=v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;
    HttpTracker publicHttp(address,false),privateHttp(address,true);UdpTracker publicUdp(address);
    Profile pub(root.path(),"public",v6,true,udp?publicUdp.url():publicHttp.url());
    Profile priv(root.path(),"private",v6,true,privateHttp.url());makePrivate(priv,privateFlag);
    publicHttp.expect(pub);publicUdp.expect(pub);privateHttp.expect(priv);
    auto inventory=entry(pub.path+"/downloads.config");auto privateInventory=entry(priv.path+"/downloads.config");
    auto row=privateInventory["downloads"].list().front();row["position"]=2;inventory["downloads"].list().push_back(row);
    test::write(pub.path+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
    test::write(pub.path+"/active/"+QString::fromStdString(priv.hash.hex())+".dat",test::read(priv.state));
    test::mainline::OwnedKrpc backend(pub.peerPort,address);backend.allowWithoutPeers(pub.wireHash());if(!privateFlag){backend.allowWithoutPeers(priv.wireHash());}backend.allowAnnouncePort(pub.peerPort);
    KrpcCapture capture(address,backend.port());Host host({{v6?"::1":"127.0.0.1",capture.port()}});
    pub.releasePorts();QVERIFY(host.bridge.openNormal(pub.native()));
    const auto diagnostic=[&]{QStringList rows;if(const auto snapshot=host.snapshot()){for(const auto& item:snapshot->items){rows<<item.name+":"+item.state;}}return host.diagnostic()+QStringLiteral(" rows=%1 privateHTTP=%2 publicHTTP=%3 publicUDP=%4 gets=%5 announces=%6").arg(rows.join('|')).arg(privateHttp.announces).arg(publicHttp.announces).arg(publicUdp.announces).arg(backend.gets(pub.wireHash())).arg(backend.announced(pub.wireHash()))+backend.failure+privateHttp.failure+publicHttp.failure+publicUdp.failure+capture.failure;};
    const auto active=[&]{return host.ready(pub)&&host.row(pub)->state.contains("Seeding")&&host.row(priv)&&host.row(priv)->state.contains("Seeding")&&privateHttp.announces>0&&(udp?publicUdp.announces:publicHttp.announces)>0&&backend.gets(pub.wireHash())>0&&backend.announced(pub.wireHash())>0&&(privateFlag||(backend.gets(priv.wireHash())>0&&backend.announced(priv.wireHash())>0));};
    QTRY_VERIFY2_WITH_TIMEOUT(active(),qPrintable(diagnostic()),15000);
    QVERIFY(host.bridge.closeSession());QTRY_VERIFY_WITH_TIMEOUT(closedAndReleased(host),15000);
    QVERIFY(!capture.queries.empty());QVERIFY(noCredential(capture.queries));
    QVERIFY(noCredential(publicHttp.requests));QVERIFY(noCredential(publicUdp.requests));
    QVERIFY(!privateHttp.requests.empty());QVERIFY(!noCredential(privateHttp.requests));
    if(privateFlag){QCOMPARE(backend.gets(priv.wireHash()),0);QCOMPARE(backend.announced(priv.wireHash()),0);}
    for(const auto& frame:capture.queries){
      if(privateFlag){QVERIFY(!frame.contains(hashBytes(priv)));}QVERIFY(!frame.contains(privateHttp.url().toUtf8()));
      const auto parsed=core::Document::parse(frame.toStdString());QVERIFY(parsed);const auto* q=parsed->find("q");const auto* a=parsed->find("a");QVERIFY(q&&a);
      if(q->bytes=="get_peers"||q->bytes=="announce_peer"){
        const auto* hash=a->find("info_hash");QVERIFY(hash);
        // Libtorrent also uses random get_peers targets to refresh routing buckets.
        if(q->bytes=="announce_peer"){const QByteArray observed(hash->bytes.data(),hash->bytes.size());QVERIFY(observed==hashBytes(pub)||(!privateFlag&&observed==hashBytes(priv)));}

      }
    }
    QVERIFY2(backend.failure.isEmpty(),qPrintable(backend.failure));QVERIFY2(capture.failure.isEmpty(),qPrintable(capture.failure));
    QVERIFY2(privateHttp.failure.isEmpty(),qPrintable(privateHttp.failure));QVERIFY2(publicHttp.failure.isEmpty(),qPrintable(publicHttp.failure));QVERIFY2(publicUdp.failure.isEmpty(),qPrintable(publicUdp.failure));
    QCOMPARE(test::read(pub.data),pub.body);QCOMPARE(test::read(priv.data),priv.body);
  }
};
QTEST_MAIN(TrackerPrivacyTests)
#include "tracker_privacy_tests.moc"
