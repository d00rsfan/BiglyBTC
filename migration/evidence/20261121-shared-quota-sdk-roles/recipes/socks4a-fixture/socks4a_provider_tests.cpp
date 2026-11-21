// Qualification-only actual numeric-loopback proxy/origin. Never a shipping
// resolver or generic relay; root must approve the exact newly built provider.
#include <libtorrent/biglybtc_socks4a.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <QFile>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <dlfcn.h>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>
namespace lt=libtorrent;
namespace {
class Origin final:public QObject {
public:
 QTcpServer server{this};QByteArray bytes;unsigned requests=0;bool badSource=false;
 explicit Origin(bool http) {
  server.setProxy(QNetworkProxy::NoProxy);
  if(!server.listen(QHostAddress::LocalHost,0)){qFatal("Owned origin listen failed");}
  connect(&server,&QTcpServer::newConnection,this,[this,http]{while(server.hasPendingConnections()){
   auto* socket=server.nextPendingConnection();badSource|=socket->peerAddress()!=QHostAddress("127.0.0.3");
   connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
   connect(socket,&QTcpSocket::readyRead,socket,[this,socket,http]{bytes+=socket->readAll();if(bytes.size()>16384){qFatal("Owned request exceeded bound");}
    if(!http||!bytes.contains("\r\n\r\n")){return;}++requests;
    if(!bytes.startsWith("GET /announce?")||!bytes.contains("info_hash=")){qFatal("Unexpected owned tracker request");}
    const QByteArray body="d8:intervali3600e5:peers0:e";socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
   });
  }});
 }
};
class Proxy final:public QObject {
 struct Channel {QPointer<QTcpSocket> client,upstream;QByteArray input;bool opening=false,relay=false;};
 QByteArray expectedHost_;quint16 expectedPort_;bool refuse_;
public:
 QTcpServer server{this};QByteArray raw,user,host,failure;unsigned requests=0;bool domain=false;
 Proxy(QByteArray hostName,quint16 port,bool refuse=false):expectedHost_(std::move(hostName)),expectedPort_(port),refuse_(refuse){
  server.setProxy(QNetworkProxy::NoProxy);if(!server.listen(QHostAddress::LocalHost,0)){qFatal("Owned proxy listen failed");}
  connect(&server,&QTcpServer::newConnection,this,[this]{while(server.hasPendingConnections()){
   auto channel=std::make_shared<Channel>();channel->client=server.nextPendingConnection();
   connect(channel->client,&QTcpSocket::disconnected,channel->client,&QObject::deleteLater);
   connect(channel->client,&QTcpSocket::readyRead,channel->client,[this,channel]{receive(channel);});
  }});
 }
private:
 void fail(const std::shared_ptr<Channel>& c,const char* why){failure=why;if(c->client){c->client->abort();}}
 void receive(const std::shared_ptr<Channel>& c){
  if(!c->client){return;}if(c->relay){if(c->upstream){c->upstream->write(c->client->readAll());}return;}
  c->input+=c->client->readAll();if(c->input.size()>1024){fail(c,"Proxy frame bound");return;}if(c->opening||c->input.size()<9){return;}
  auto& data=c->input;const auto octet=[&](qsizetype n){return static_cast<unsigned char>(data[n]);};
  if(octet(0)!=4||octet(1)!=1){fail(c,"Expected genuine SOCKS4 CONNECT");return;}
  const auto userEnd=data.indexOf('\0',8);if(userEnd<0){return;}
  domain=data.mid(4,4)==QByteArray::fromHex("00000001");auto consumed=userEnd+1;
  const QByteArray candidateUser=data.mid(8,userEnd-8);QByteArray candidateHost;
  if(domain){const auto hostEnd=data.indexOf('\0',consumed);if(hostEnd<0){return;}candidateHost=data.mid(consumed,hostEnd-consumed);consumed=hostEnd+1;}
  else{candidateHost=QHostAddress((static_cast<quint32>(octet(4))<<24)|(static_cast<quint32>(octet(5))<<16)|(static_cast<quint32>(octet(6))<<8)|octet(7)).toString().toLatin1();}
  if(candidateUser!="owned-user"||candidateHost!=expectedHost_||static_cast<quint16>((octet(2)<<8)|octet(3))!=expectedPort_){fail(c,"Unexpected proxy identity or destination");return;}
  if(++requests>16){fail(c,"Owned proxy request bound");return;}raw=data.left(consumed);user=candidateUser;host=candidateHost;data.remove(0,consumed);c->opening=true;
  if(refuse_){c->client->write(QByteArray::fromHex("005b000000000000"));c->client->disconnectFromHost();return;}
  auto* upstream=new QTcpSocket(c->client);c->upstream=upstream;upstream->setProxy(QNetworkProxy::NoProxy);
  if(!upstream->bind(QHostAddress("127.0.0.3"),0)){fail(c,"Proxy source bind failed");return;}
  connect(upstream,&QTcpSocket::connected,c->client,[c]{if(!c->client||!c->upstream){return;}c->client->write(QByteArray::fromHex("005a000000000000"));c->relay=true;if(!c->input.isEmpty()){c->upstream->write(c->input);c->input.clear();}});
  connect(upstream,&QTcpSocket::readyRead,c->client,[c]{if(c->client&&c->upstream){c->client->write(c->upstream->readAll());}});
  connect(upstream,&QTcpSocket::disconnected,c->client,[c]{if(c->client){c->client->disconnectFromHost();}});
  connect(c->client,&QTcpSocket::disconnected,upstream,[c]{if(c->upstream){c->upstream->abort();}});
  // Exact fixture route only: no lookup or generic destination forwarding.
  upstream->connectToHost(QHostAddress::LocalHost,expectedPort_);
 }
};
std::shared_ptr<lt::torrent_info> metadata(){
 lt::entry value(lt::entry::dictionary_type{});auto& info=value["info"];info["name"]="owned.bin";info["length"]=1;info["piece length"]=16384;info["pieces"]=lt::hasher("q",1).final().to_string();
 std::vector<char> bytes;lt::bencode(std::back_inserter(bytes),value);return std::make_shared<lt::torrent_info>(bytes.data(),static_cast<int>(bytes.size()));
}
}
class Socks4aProviderTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){
  QCOMPARE(lt::biglybtc_socks4a_abi_v1(),1U);auto* marker=::dlsym(RTLD_DEFAULT,"biglybtc_socks4a_abi_v1");auto* version=::dlsym(RTLD_DEFAULT,"_ZN10libtorrent7versionEv");Dl_info a{},b{};
  QVERIFY(marker&&version&&::dladdr(marker,&a)&&::dladdr(version,&b));QCOMPARE(a.dli_fbase,b.dli_fbase);
 }
 void actualHttpFactoryRoutes_data(){
  QTest::addColumn<bool>("remote");QTest::addColumn<bool>("refuse");QTest::addColumn<QByteArray>("destination");
  QTest::newRow("numeric4-USERID")<<false<<false<<QByteArray("127.0.0.1");
  QTest::newRow("remote4a-unresolvable-HTTP")<<true<<false<<QByteArray("owned.fixture.invalid");
  QTest::newRow("remote4a-localhost-HTTP")<<true<<false<<QByteArray("localhost");
  QTest::newRow("remote4a-refused")<<true<<true<<QByteArray("owned.fixture.invalid");
 }
 void actualHttpFactoryRoutes(){
  QFETCH(bool,remote);QFETCH(bool,refuse);QFETCH(QByteArray,destination);Origin origin(true);Proxy proxy(destination,origin.server.serverPort(),refuse);QTemporaryDir root("/tmp/biglybtc-socks4a-XXXXXX");QVERIFY(root.isValid());QFile payload(root.path()+"/owned.bin");QVERIFY(payload.open(QIODevice::WriteOnly));QCOMPARE(payload.write("q",1),qint64(1));payload.close();
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");settings.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
  for(const auto key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp}){settings.set_bool(key,false);}
  // This isolated qualification tracker is deliberately loopback; production SSRF policy is unchanged.
  settings.set_bool(lt::settings_pack::ssrf_mitigation,false);
  settings.set_int(lt::settings_pack::proxy_type,lt::settings_pack::socks4);settings.set_str(lt::settings_pack::proxy_hostname,"127.0.0.1");settings.set_int(lt::settings_pack::proxy_port,proxy.server.serverPort());settings.set_str(lt::settings_pack::proxy_username,"owned-user");settings.set_bool(lt::settings_pack::proxy_hostnames,remote);settings.set_bool(lt::settings_pack::proxy_tracker_connections,true);settings.set_bool(lt::settings_pack::proxy_peer_connections,false);settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(static_cast<std::uint32_t>(lt::alert_category::all)));
  lt::session owner{lt::session_params(settings)};lt::add_torrent_params params;params.ti=metadata();params.save_path=root.path().toStdString();params.trackers={std::string("http://")+destination.toStdString()+":"+std::to_string(origin.server.serverPort())+"/announce"};params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);params.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;lt::error_code error;auto handle=owner.add_torrent(std::move(params),error);QVERIFY2(!error&&handle.is_valid(),error.message().c_str());
  bool reply=false,failed=false;const auto observed=[&]{std::vector<lt::alert*> batch;owner.pop_alerts(&batch);for(const auto* alert:batch){reply|=lt::alert_cast<lt::tracker_reply_alert>(alert)!=nullptr;failed|=lt::alert_cast<lt::tracker_error_alert>(alert)!=nullptr;}return refuse?failed:reply;};
  QTRY_VERIFY_WITH_TIMEOUT(observed(),10000);QVERIFY2(proxy.failure.isEmpty(),proxy.failure.constData());QVERIFY(proxy.requests>0);QCOMPARE(proxy.domain,remote);QCOMPARE(proxy.user,QByteArray("owned-user"));QCOMPARE(proxy.host,destination);
  QByteArray expected=QByteArray::fromHex("0401");expected+=static_cast<char>(origin.server.serverPort()>>8);expected+=static_cast<char>(origin.server.serverPort());expected+=QByteArray::fromHex(remote?"00000001":"7f000001");expected+="owned-user";expected+='\0';if(remote){expected+=destination;expected+='\0';}QCOMPARE(proxy.raw,expected);
  if(refuse){QVERIFY(!reply);QCOMPARE(origin.requests,0U);QVERIFY(origin.bytes.isEmpty());}else{QVERIFY(!failed);QVERIFY(origin.requests>0);QVERIFY(!origin.badSource);}owner.remove_torrent(handle);
 }
};
QTEST_GUILESS_MAIN(Socks4aProviderTests)
#include "socks4a_provider_tests.moc"
