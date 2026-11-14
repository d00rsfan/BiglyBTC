// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QTemporaryDir>
#include <QFile>
#include <QUrlQuery>
#include <QtEndian>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/aux_/ip_helpers.hpp>
namespace lt=libtorrent;
namespace {
QByteArray integer(quint32 n){QByteArray b(4,0);qToBigEndian(n,b.data());return b;}
}
class TrackerAnnounceAddressTests final:public QObject {
 Q_OBJECT
private slots:
 void originalSDKFiltersPrivateAndLinkLocalExplicitAddresses(){
  for(const char* raw:{"10.253.1.7","172.31.254.7","192.168.254.7","169.254.17.7","fe80::1234","fc00::1234"}){QVERIFY(lt::aux::is_local(lt::make_address(raw)));}
 }
 void actualRequestLetsTrackerObserveSourceAddress_data(){
  QTest::addColumn<bool>("v6");QTest::addColumn<bool>("udp");QTest::addColumn<bool>("privateTorrent");
  for(bool v6:{false,true}){for(bool udp:{false,true}){for(bool priv:{false,true}){const QByteArray name=QByteArray(v6?"IPv6":"IPv4")+(udp?"-UDP":"-HTTP")+(priv?"-private":"-public");QTest::newRow(name.constData())<<v6<<udp<<priv;}}}
 }
 void actualRequestLetsTrackerObserveSourceAddress(){
  QFETCH(bool,v6);QFETCH(bool,udp);QFETCH(bool,privateTorrent);const QHostAddress bind(v6?"::1":"127.0.0.1");QTcpServer http;QUdpSocket datagrams;bool listening=udp?datagrams.bind(bind,0):http.listen(bind,0);if(!listening&&v6){QSKIP("Owned IPv6 loopback unavailable");}QVERIFY(listening);const quint16 port=udp?datagrams.localPort():http.serverPort();std::vector<QByteArray> observed;bool invalid=false;
  connect(&http,&QTcpServer::newConnection,&http,[&]{while(http.hasPendingConnections()){auto* socket=http.nextPendingConnection();auto pending=std::make_shared<QByteArray>();connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{*pending+=socket->readAll();if(!pending->contains("\r\n\r\n")){return;}const auto first=pending->left(pending->indexOf("\r\n"));const auto words=first.split(' ');if(words.size()!=3||!words[1].startsWith("/announce?")){invalid=true;socket->abort();return;}observed.push_back(words[1]);const QByteArray body="d8:intervali3600e5:peers0:6:peers60:e";socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();pending->clear();});}});
  connect(&datagrams,&QUdpSocket::readyRead,&datagrams,[&]{while(datagrams.hasPendingDatagrams()){auto d=datagrams.receiveDatagram();const auto bytes=d.data();if(bytes.size()<16){invalid=true;continue;}const auto action=qFromBigEndian<quint32>(bytes.constData()+8);QByteArray reply;if(action==0){reply=integer(0)+bytes.mid(12,4)+QByteArray::fromHex("0102030405060708");}else if(action==1&&bytes.size()>=98){observed.push_back(bytes);reply=integer(1)+bytes.mid(12,4)+integer(3600)+integer(0)+integer(1);}else{invalid=true;continue;}datagrams.writeDatagram(reply,d.senderAddress(),d.senderPort());}});
  QTemporaryDir root;QVERIFY(root.isValid());QFile file(root.path()+"/generated.bin");QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("abc"),qint64{3});file.close();
  lt::file_storage files;files.add_file("generated.bin",3);lt::create_torrent creator(files,16384,lt::create_torrent::v1_only);const auto pieceHash=QCryptographicHash::hash("abc",QCryptographicHash::Sha1);creator.set_hash(0,lt::sha1_hash(pieceHash.constData()));creator.set_priv(privateTorrent);std::vector<char> encoded;lt::bencode(std::back_inserter(encoded),creator.generate());lt::error_code ec;auto metadata=std::make_shared<lt::torrent_info>(lt::span<char const>(encoded),ec,lt::from_span);QVERIFY(!ec);
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,v6?"[::1]:0":"127.0.0.1:0");settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);lt::session sdk(settings);QCOMPARE(sdk.get_settings().get_str(lt::settings_pack::announce_ip),std::string{});
  lt::add_torrent_params params;params.ti=std::move(metadata);params.save_path=root.path().toStdString();params.flags=lt::torrent_flags::seed_mode;params.trackers={(udp?std::string("udp://"):std::string("http://"))+(v6?"[::1]":"127.0.0.1")+":"+std::to_string(port)+"/announce"};auto handle=sdk.add_torrent(std::move(params),ec);QVERIFY(!ec&&handle.is_valid());QTRY_VERIFY_WITH_TIMEOUT(!observed.empty(),6000);QVERIFY(!invalid);
  for(const auto& packet:observed){if(udp){QCOMPARE(packet.mid(84,4),QByteArray(4,0));}else{QUrlQuery query(QUrl::fromEncoded(packet));QVERIFY(!query.hasQueryItem("ip"));QVERIFY(!query.hasQueryItem("ipv4"));QVERIFY(!query.hasQueryItem("ipv6"));}}
 }
};
QTEST_GUILESS_MAIN(TrackerAnnounceAddressTests)
#include "tracker_announce_address_tests.moc"
