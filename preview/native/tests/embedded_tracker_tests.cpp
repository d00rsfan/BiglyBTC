// SPDX-License-Identifier: GPL-2.0-or-later
#include "embedded_tracker.hpp"
#include "tracker_protocol.hpp"
#include "biglybtc/bencode.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtEndian>
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#include <unistd.h>
using namespace biglybtc;namespace t=tracker;namespace wire=tracker::detail;
namespace {
thread_local int failSync=0,syncCalls=0;
void must(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
QByteArray b(QByteArray value){return QByteArray::number(value.size())+':'+value;}
QByteArray dict(std::initializer_list<std::pair<QByteArray,QByteArray>> values){std::map<QByteArray,QByteArray> sorted(values.begin(),values.end());QByteArray result="d";for(const auto& [key,value]:sorted){result+=b(key)+value;}return result+'e';}
void write(const QString& file,const QByteArray& bytes){QFile out(file);must(out.open(QIODevice::WriteOnly|QIODevice::Truncate)&&out.write(bytes)==bytes.size()&&out.flush(),"Cannot write private tracker fixture");}
QByteArray bytes(const QString& path){QFile file(path);must(file.open(QIODevice::ReadOnly),"Cannot read private tracker fixture");return file.readAll();}
core::Document document(const QByteArray& value){auto result=core::Document::parse(value.toStdString());must(bool(result),"Cannot parse actual tracker fixture");return std::move(*result);}
std::string raw(const core::Document& doc,const core::Node& node){return doc.originalBytes().substr(node.begin,node.end-node.begin);}
QByteArray encoded(const std::string& value){constexpr char digits[]="0123456789ABCDEF";QByteArray out;for(const auto ch:value){const auto c=static_cast<unsigned char>(ch);out+='%';out+=digits[c>>4];out+=digits[c&15];}return out;}
QByteArray http(const QByteArray& target){return "GET "+target+" HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";}
QByteArray announce(const std::string& hash,const std::string& peer,quint16 port,const char* event="started",qint64 left=1,qint64 up=0,qint64 down=0,const QByteArray& extra="&compact=1"){return http("/announce?info_hash="+encoded(hash)+"&peer_id="+encoded(peer)+"&port="+QByteArray::number(port)+"&uploaded="+QByteArray::number(up)+"&downloaded="+QByteArray::number(down)+"&left="+QByteArray::number(left)+"&event="+event+extra);}
struct Profile {
 QTemporaryDir root;QString path,metadataFile;std::optional<storage::ProfileLease> lease;std::string hash;
 explicit Profile(bool record=true){must(root.isValid(),"No private tracker root");path=root.filePath("profile");must(QDir().mkpath(path+"/torrents"),"No private tracker profile");write(path+"/biglybt.config",dict({{"opaque",dict({{"retained",b("global-unknown")}})}}));
  const auto info=dict({{"length","i1e"},{"name",b("owned.bin")},{"piece length","i16384e"},{"pieces",b(QByteArray(20,'x'))}});const auto metadata=dict({{"info",info},{"opaque",b("cache-unknown")}});const auto digest=QCryptographicHash::hash(info,QCryptographicHash::Sha1);hash=digest.toStdString();metadataFile=path+"/torrents/owned.torrent";write(metadataFile,metadata);
  if(record){const auto stats=dict({{"completed","i0e"},{"announces","i0e"},{"scrapes","i0e"},{"uploaded","i0e"},{"downloaded","i0e"},{"bytesin","i0e"},{"bytesout","i0e"},{"future",b("stats-unknown")}});const auto row=dict({{"persistent","i1e"},{"passive","i1e"},{"hash",b(digest)},{"status","i1e"},{"dateadded","i123e"},{"torrent_file",b(metadataFile.toLatin1())},{"stats",stats},{"row_future",b("row-unknown")}});write(path+"/tracker.config",dict({{"torrents","l"+row+"e"},{"per_key",b("opaque-peer-key")},{"future",b("root-unknown")}}));}
  auto held=storage::ProfileLease::acquire(path.toStdString());must(bool(held),"No actual private tracker lease");lease.emplace(std::move(*held));
 }
};
std::unique_ptr<t::EmbeddedTrackerService> service(Profile& p,t::TrackerServiceMode mode=t::TrackerServiceMode::Normal){auto result=t::EmbeddedTrackerService::open(*p.lease,p.path.toStdString(),501,mode);must(bool(result),"Cannot open actual tracker service");return std::move(*result);}
t::OwnedTrackerEndpoint startEndpoint(Profile& p,t::EmbeddedTrackerService& s){must(bool(s.configure(*p.lease,501,{"127.0.0.1",0},{})),"Cannot configure actual HTTP tracker");auto endpoint=s.startListener(*p.lease,501,{});must(bool(endpoint),"Cannot start actual private listener");return std::move(*endpoint);}
QByteArray exchange(Profile& p,t::EmbeddedTrackerService& s,quint16 port,const QByteArray& request){QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,port);QElapsedTimer clock;clock.start();bool written=false;QByteArray response;while(clock.elapsed()<5000){QCoreApplication::processEvents();if(!written&&socket.state()==QAbstractSocket::ConnectedState){must(socket.write(request)==request.size(),"Private HTTP request failed");written=true;}must(bool(s.poll(*p.lease,501)),"Actual tracker poll failed");response+=socket.readAll();if(written&&socket.state()==QAbstractSocket::UnconnectedState){must(bool(s.poll(*p.lease,501)),"Actual tracker byte receipts failed");return response;}QTest::qWait(1);}throw std::runtime_error("Private tracker exchange timed out");}
struct GeneratedTrackerCertificate {QByteArray keyStore;QSslCertificate leaf;};
GeneratedTrackerCertificate generateTrackerCertificate() {
  const std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(EVP_PKEY_Q_keygen(nullptr,nullptr,"RSA",2048),&EVP_PKEY_free);
  const std::unique_ptr<X509,decltype(&X509_free)> cert(X509_new(),&X509_free);
  must(key&&cert,"Could not generate an isolated tracker certificate");
  auto* subject=X509_get_subject_name(cert.get());
  must(X509_set_version(cert.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1)==1
      &&X509_gmtime_adj(X509_getm_notBefore(cert.get()),-30)&&X509_gmtime_adj(X509_getm_notAfter(cert.get()),3600)
      &&X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("localhost"),-1,-1,0)==1
      &&X509_set_issuer_name(cert.get(),subject)==1&&X509_set_pubkey(cert.get(),key.get())==1,
      "Could not bind the generated certificate name and key");
  const std::unique_ptr<X509_EXTENSION,decltype(&X509_EXTENSION_free)> localAddress(
      X509V3_EXT_conf_nid(nullptr,nullptr,NID_subject_alt_name,"DNS:localhost,IP:127.0.0.1"),&X509_EXTENSION_free);
  must(localAddress&&X509_add_ext(cert.get(),localAddress.get(),-1)==1&&X509_sign(cert.get(),key.get(),EVP_sha256())>0,
      "Could not sign the generated tracker certificate");
  const auto certificateSize=i2d_X509(cert.get(),nullptr);
  must(certificateSize>0,"Could not export the generated tracker certificate");
  QByteArray certificate(certificateSize,Qt::Uninitialized);
  auto* output=reinterpret_cast<unsigned char*>(certificate.data());
  must(i2d_X509(cert.get(),&output)==certificateSize,"Failed to export the generated certificate");
  const auto chain=QSslCertificate::fromData(certificate,QSsl::Der);
  must(chain.size()==1&&!chain.front().isNull(),"Invalid generated tracker certificate");
  const std::unique_ptr<PKCS12,decltype(&PKCS12_free)> store(
      PKCS12_create("changeit","tracker",key.get(),cert.get(),nullptr,0,0,0,0,0),&PKCS12_free);
  must(bool(store),"Could not create the isolated tracker key store");
  const auto storeSize=i2d_PKCS12(store.get(),nullptr);
  must(storeSize>0,"Could not export the generated key store");
  QByteArray keyStore(storeSize,Qt::Uninitialized);
  output=reinterpret_cast<unsigned char*>(keyStore.data());
  must(i2d_PKCS12(store.get(),&output)==storeSize,"Failed to export generated key store");
  return {std::move(keyStore),chain.front()};
}
void udpInteger(QByteArray& bytes,quint64 value,int width) {
  for(int index=width-1;index>=0;--index){bytes+=static_cast<char>((value>>(8*index))&0xff);}
}
QByteArray udpPrefix(quint64 token,quint32 action,quint32 transaction) {
  QByteArray request;
  udpInteger(request,token,8);udpInteger(request,action,4);udpInteger(request,transaction,4);
  return request;
}
QByteArray exchangeUdp(Profile& profile,t::EmbeddedTrackerService& owner,QUdpSocket& socket,quint16 port,
    const QByteArray& request,int timeout=2000) {
  must(socket.writeDatagram(request,QHostAddress::LocalHost,port)==request.size(),"Cannot send loopback tracker datagram");
  QElapsedTimer wait;wait.start();
  while(wait.elapsed()<timeout&&!socket.hasPendingDatagrams()){
    must(bool(owner.poll(*profile.lease,501)),"Owned tracker stopped during UDP exchange");
    QTest::qWait(5);
  }
  if(!socket.hasPendingDatagrams()){return {};}
  QByteArray response(static_cast<qsizetype>(socket.pendingDatagramSize()),Qt::Uninitialized);
  must(socket.readDatagram(response.data(),response.size())==response.size(),"Cannot read loopback tracker datagram");
  return response;
}
core::Document body(const QByteArray& response){const auto end=response.indexOf("\r\n\r\n");must(response.startsWith("HTTP/1.1 200 OK")&&end>=0,"Missing actual HTTP tracker reply");return document(response.mid(end+4));}
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){if(failSync&&++syncCalls==failSync){errno=EIO;return -1;}return __real_fsync(fd);}
class EmbeddedTrackerTests final:public QObject {
 Q_OBJECT
private slots:
 void rawBinaryCodecAndMalformedRequests_data(){
  QTest::addColumn<QByteArray>("request");QTest::addColumn<bool>("valid");const std::string hash("\0+%\xff" "abcdefghijklmnop",20),peer("\0+%\xfe" "0123456789abcdef",20);const auto good=announce(hash,peer,1234);
  QTest::newRow("raw-binary-and-percent-plus")<<good<<true;QTest::newRow("dictionary-no-peer-id")<<announce(hash,peer,1234,"started",1,0,0,"&compact=0&no_peer_id=1")<<true;QTest::newRow("multi-scrape")<<http("/scrape?info_hash="+encoded(hash)+"&info_hash="+encoded(std::string(20,'z'))) <<true;
  QTest::newRow("full-scrape")<<http("/scrape")<<true;QTest::newRow("non-GET")<<QByteArray("POST /announce HTTP/1.1\r\n\r\n")<<false;QTest::newRow("unknown-path")<<http("/other")<<false;QTest::newRow("missing-peer")<<http("/announce?info_hash="+encoded(hash))<<false;QTest::newRow("short-hash")<<announce(std::string(19,'x'),peer,1234)<<false;QTest::newRow("short-peer")<<announce(hash,std::string(19,'p'),1234)<<false;QTest::newRow("zero-port")<<announce(hash,peer,0)<<false;QTest::newRow("negative-counter")<<announce(hash,peer,1234,"started",-1)<<false;QTest::newRow("duplicate-port")<<announce(hash,peer,1234,"started",1,0,0,"&port=99")<<false;QTest::newRow("duplicate-announce-hash")<<announce(hash,peer,1234,"started",1,0,0,"&info_hash="+encoded(hash))<<false;QTest::newRow("malformed-percent")<<http("/scrape?info_hash=%QZ")<<false;QTest::newRow("raw-key-not-percent-decoded")<<http("/scrape?%69nfo_hash="+encoded(hash))<<false;QTest::newRow("advanced-compact")<<announce(hash,peer,1234,"started",1,0,0,"&compact=2")<<false;QTest::newRow("counter-overflow")<<announce(hash,peer,1234,"started",1,0,0,"&numwant=9999999999999999999999")<<false;QTest::newRow("body-or-pipeline")<<(good+"x")<<false;QTest::newRow("header-oversize")<<(good+QByteArray(8192,'a'))<<false;QTest::newRow("duplicate-header")<<QByteArray("GET /scrape HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n")<<false;
 }
 void rawBinaryCodecAndMalformedRequests(){QFETCH(QByteArray,request);QFETCH(bool,valid);auto parsed=wire::parseTrackerRequest(request.toStdString());QCOMPARE(bool(parsed),valid);if(parsed&&parsed->announce){QCOMPARE(parsed->hashes[0].size(),std::size_t{20});QCOMPARE(parsed->peerId.size(),std::size_t{20});QCOMPARE(parsed->hashes[0][0],'\0');QCOMPARE(parsed->hashes[0][1],'+');}}
 void purePeerKeyCompactFullScrapeAndExpiryValues(){
  const std::string hash(20,'h'),first(20,'a'),second(20,'b');std::map<std::string,wire::WireTorrent> registry;registry[hash].serving=true;wire::WirePolicy policy;const auto now=std::chrono::steady_clock::now();auto request=wire::parseTrackerRequest(announce(hash,first,1111,"started",0).toStdString());QVERIFY(request);auto initial=wire::trackerResponse(*request,{127,0,0,1},registry,policy,now);QVERIFY(!document(QByteArray::fromStdString(initial.body)).find("failure reason"));QCOMPARE(registry[hash].stats.completed,std::int64_t{0});
  request=wire::parseTrackerRequest(announce(hash,second,2222,"started",2,0,0,"&compact=0&key=real").toStdString());QVERIFY(request);auto listed=wire::trackerResponse(*request,{127,0,0,1},registry,policy,now);auto list=document(QByteArray::fromStdString(listed.body));QCOMPARE(list.find("peers")->type,core::Node::Type::List);QCOMPARE(list.find("peers")->children.size(),std::size_t{1});QCOMPARE(registry[hash].stats.seeds,std::uint32_t{1});QCOMPARE(registry[hash].stats.leechers,std::uint32_t{1});
  request->key="foreign";const auto before=registry[hash].stats.announces;auto rejected=wire::trackerResponse(*request,{127,0,0,1},registry,policy,now);QVERIFY(document(QByteArray::fromStdString(rejected.body)).find("failure reason"));QCOMPARE(registry[hash].stats.announces,before);request->key="real";request->event="completed";request->left=0;wire::trackerResponse(*request,{127,0,0,1},registry,policy,now+std::chrono::seconds(1));wire::trackerResponse(*request,{127,0,0,1},registry,policy,now+std::chrono::seconds(2));QCOMPARE(registry[hash].stats.completed,std::int64_t{1});
  wire::WireRequest scrape;scrape.fullScrape=true;policy.fullScrape=false;QVERIFY(document(QByteArray::fromStdString(wire::trackerResponse(scrape,{127,0,0,1},registry,policy,now).body)).find("failure reason"));policy.fullScrape=true;auto expired=wire::trackerResponse(scrape,{127,0,0,1},registry,policy,now+std::chrono::seconds(policy.interval*3+3));QVERIFY(!document(QByteArray::fromStdString(expired.body)).find("failure reason"));QVERIFY(registry[hash].peers.empty());QCOMPARE(registry[hash].stats.seeds,std::uint32_t{0});
 }
 void emptyProfileExplicitEndpointAndBackgroundLifetime(){
  {Profile saved;auto initial=document(bytes(saved.path+"/tracker.config"));auto row=document(QByteArray::fromStdString(raw(initial,initial.find("torrents")->children[0])));QVERIFY(row.replaceValue("torrent_file",b((saved.path+"/missing-parent/unavailable.torrent").toLatin1()).toStdString()));QVERIFY(initial.replaceValue("torrents","l"+row.serialize()+"e"));write(saved.path+"/tracker.config",QByteArray::fromStdString(initial.serialize()));const auto original=bytes(saved.path+"/tracker.config");auto preview=service(saved,t::TrackerServiceMode::Preview);auto observed=preview->catalog(*saved.lease,501);QVERIFY(observed&&observed->saved&&observed->rows.size()==1&&!observed->rows[0].eligible&&!observed->rows[0].runtime&&!observed->listener.running);QCOMPARE(observed->rows[0].reason,std::string("Read-only preview: saved record only; no metadata path inspection"));QCOMPARE(bytes(saved.path+"/tracker.config"),original);QVERIFY(!QFile::exists(saved.path+"/missing-parent"));QVERIFY(preview->close().leaseReleased);}
  Profile area(false);const auto original=bytes(area.path+"/biglybt.config");{auto preview=service(area,t::TrackerServiceMode::Preview);auto catalog=preview->catalog(*area.lease,501);QVERIFY(catalog&&!catalog->listener.running&&catalog->listener.joined&&!catalog->configuredEndpoint);QVERIFY(!preview->configure(*area.lease,501,{"127.0.0.1",0},{}));QVERIFY(!preview->startListener(*area.lease,501,{}));QCOMPARE(bytes(area.path+"/biglybt.config"),original);QVERIFY(preview->close().leaseReleased);}
  auto owner=service(area);QVERIFY(!owner->configure(*area.lease,501,{"0.0.0.0",0},{}));QVERIFY(!owner->configure(*area.lease,501,{"224.0.0.1",6969},{}));QVERIFY(owner->configure(*area.lease,501,{"127.0.0.1",0},{}));auto configured=owner->catalog(*area.lease,501);QVERIFY(configured&&configured->configuredEndpoint&&configured->configuredEndpoint->port==0&&!configured->listener.running);std::stop_source cancelled;cancelled.request_stop();QVERIFY(!owner->startListener(*area.lease,501,cancelled.get_token()));std::stop_source request;auto endpoint=owner->startListener(*area.lease,501,request.get_token());QVERIFY(endpoint&&endpoint->description().port>0);QCOMPARE(endpoint->description().announceUrl,"http://127.0.0.1:"+std::to_string(endpoint->description().port)+"/announce");request.request_stop();QVERIFY(endpoint->current(*area.lease,501));
  auto saved=document(bytes(area.path+"/biglybt.config"));QCOMPARE(saved.find("Tracker Port")->integer,std::int64_t(endpoint->description().port));QCOMPARE(saved.find("Tracker Port Enable")->integer,std::int64_t{1});QCOMPARE(saved.find("Tracker NAT Check Enable")->integer,std::int64_t{0});QCOMPARE(saved.find("Sharing Protocol")->bytes,std::string_view("HTTP"));auto old=document(original);QCOMPARE(raw(saved,*saved.find("opaque")),raw(old,*old.find("opaque")));QVERIFY(owner->stopListener(*area.lease,501,{}));QVERIFY(!endpoint->current(*area.lease,501));const auto receipt=owner->close();QVERIFY(receipt.listenerJoined&&receipt.connectionsJoined&&receipt.leaseReleased);area.lease.reset();QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void realTcpOwnedHostAnnounceScrapeStopAndCheckpoint(){
  Profile area;const auto original=document(bytes(area.path+"/tracker.config"));auto owner=service(area);auto endpoint=startEndpoint(area,*owner);auto catalog=owner->catalog(*area.lease,501);QVERIFY(catalog&&catalog->sourceKind==storage::RecoverySource::Primary&&catalog->rows.size()==1&&catalog->rows[0].eligible);auto started=owner->start(*area.lease,501,catalog->rows[0].key,{});QVERIFY(started&&started->rows[0].serving);
  QTcpServer seed,leecher;QVERIFY(seed.listen(QHostAddress::LocalHost,0));QVERIFY(leecher.listen(QHostAddress::LocalHost,0));const std::string a(20,'a'),p("\0+%\xffpeer-b-0123456789",20);const auto port=endpoint.description().port;
  auto seedReply=body(exchange(area,*owner,port,announce(area.hash,a,seed.serverPort(),"started",0)));QCOMPARE(seedReply.find("complete")->integer,std::int64_t{1});auto added=body(exchange(area,*owner,port,announce(area.hash,p,leecher.serverPort(),"started",17)));QCOMPARE(added.find("complete")->integer,std::int64_t{1});QCOMPARE(added.find("incomplete")->integer,std::int64_t{1});QVERIFY(added.find("peers")->type==core::Node::Type::Bytes);QCOMPARE(added.find("peers")->bytes.size(),std::size_t{6});const auto compact=added.find("peers")->bytes;QCOMPARE(static_cast<unsigned char>(compact[4]),static_cast<unsigned char>(seed.serverPort()>>8));QCOMPARE(static_cast<unsigned char>(compact[5]),static_cast<unsigned char>(seed.serverPort()&255));
  body(exchange(area,*owner,port,announce(area.hash,p,leecher.serverPort(),"completed",0,3,17)));body(exchange(area,*owner,port,announce(area.hash,p,leecher.serverPort(),"completed",0,3,17)));auto scrape=body(exchange(area,*owner,port,http("/scrape?info_hash="+encoded(area.hash)+"&info_hash="+encoded(std::string(20,'z')))));const auto* files=scrape.find("files");QVERIFY(files&&files->find(area.hash));QCOMPARE(files->find(area.hash)->find("downloaded")->integer,std::int64_t{1});QCOMPARE(files->find(std::string(20,'z'))->find("complete")->integer,std::int64_t{0});auto stopped=body(exchange(area,*owner,port,announce(area.hash,p,leecher.serverPort(),"stopped",0,3,17)));QVERIFY(stopped.find("peers")->bytes.empty());auto unknown=body(exchange(area,*owner,port,announce(std::string(20,'z'),a,seed.serverPort())));QVERIFY(unknown.find("failure reason"));
  auto final=owner->catalog(*area.lease,501);QVERIFY(final&&final->rows[0].runtime);QCOMPARE(final->rows[0].runtime->completed,std::int64_t{1});QVERIFY(final->rows[0].runtime->announces>=5&&final->rows[0].runtime->bytesIn>0&&final->rows[0].runtime->bytesOut>0);QVERIFY(owner->stop(*area.lease,501,final->rows[0].key,{}));auto denied=body(exchange(area,*owner,port,announce(area.hash,a,seed.serverPort())));QVERIFY(denied.find("failure reason"));QVERIFY(owner->stopListener(*area.lease,501,{}));const auto closed=owner->close();QVERIFY(closed.listenerJoined&&closed.connectionsJoined&&closed.leaseReleased&&!closed.checkpointFailure);
  auto saved=document(bytes(area.path+"/tracker.config"));QCOMPARE(raw(saved,*saved.find("per_key")),raw(original,*original.find("per_key")));QCOMPARE(raw(saved,*saved.find("future")),raw(original,*original.find("future")));const auto& row=saved.find("torrents")->children[0];QCOMPARE(row.find("row_future")->bytes,std::string_view("row-unknown"));QCOMPARE(row.find("stats")->find("future")->bytes,std::string_view("stats-unknown"));QCOMPARE(row.find("stats")->find("completed")->integer,std::int64_t{1});QCOMPARE(row.find("status")->integer,std::int64_t{1});area.lease.reset();QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void realHttpsTrackerUsesTheProfileCertificate() {
  Profile area;
  const auto fixture=generateTrackerCertificate();
  const auto keyStore=area.path+"/.keystore";
  write(keyStore,fixture.keyStore);
  auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");
  QVERIFY(config&&config->document().replaceValue("Tracker Port SSL Enable","i1e"));
  QVERIFY(config->document().replaceValue("Tracker Port SSL","i0e")&&config->save(*area.lease));
  auto owner=service(area);
  auto endpoint=startEndpoint(area,*owner);
  auto catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&owner->start(*area.lease,501,catalog->rows[0].key,{}));
  const QUrl url(QString::fromStdString(endpoint.description().httpsAnnounceUrl));
  QCOMPARE(url.scheme(),QStringLiteral("https"));
  QCOMPARE(url.host(),QStringLiteral("127.0.0.1"));
  QVERIFY(url.port()>0&&url.port()!=endpoint.description().port);

  QSslSocket socket;
  auto secure=QSslConfiguration::defaultConfiguration();
  secure.setCaCertificates({fixture.leaf});
  secure.setPeerVerifyMode(QSslSocket::VerifyPeer);
  socket.setSslConfiguration(secure);
  socket.connectToHostEncrypted(url.host(),static_cast<quint16>(url.port()));
  QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
  QCOMPARE(socket.peerCertificate(),fixture.leaf);
  const auto request=announce(area.hash,std::string(20,'s'),5005,"started",0);
  QCOMPARE(socket.write(request),request.size());
  QByteArray response;
  QElapsedTimer elapsed;elapsed.start();
  while(elapsed.elapsed()<5000&&socket.state()!=QAbstractSocket::UnconnectedState){
    QVERIFY(owner->poll(*area.lease,501));
    QTest::qWait(5);response+=socket.readAll();
  }
  QCOMPARE(body(response).find("complete")->integer,std::int64_t{1});
  const auto plain=body(exchange(area,*owner,endpoint.description().port,http("/scrape?info_hash="+encoded(area.hash))));
  QCOMPARE(plain.find("files")->find(area.hash)->find("complete")->integer,std::int64_t{1});
  write(keyStore,"replacement key store");
  QVERIFY(!owner->poll(*area.lease,501));
  QVERIFY(!endpoint.current(*area.lease,501));
  const auto closed=owner->close();
  QVERIFY(closed.listenerJoined&&closed.connectionsJoined&&closed.leaseReleased);
 }
 void realUdpTrackerUsesSameOwnedSwarmAsHttp() {
  Profile area;
  auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");
  QVERIFY(config&&config->document().replaceValue("Tracker Port UDP Enable","i1e")&&config->save(*area.lease));
  auto owner=service(area);
  auto endpoint=startEndpoint(area,*owner);
  auto catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&owner->start(*area.lease,501,catalog->rows[0].key,{}));
  const quint16 port=endpoint.description().port;
  QCOMPARE(endpoint.description().udpAnnounceUrl,"udp://127.0.0.1:"+std::to_string(port)+"/announce");
  QUdpSocket seeder,leecher,foreign;
  QVERIFY(seeder.bind(QHostAddress::LocalHost,0));
  QVERIFY(leecher.bind(QHostAddress::LocalHost,0));
  QVERIFY(foreign.bind(QHostAddress::LocalHost,0));
  const auto connect=udpPrefix(0x41727101980,0,0x10203040);
  const auto seedConnect=exchangeUdp(area,*owner,seeder,port,connect);
  const auto leechConnect=exchangeUdp(area,*owner,leecher,port,connect);
  QCOMPARE(seedConnect.size(),16);
  QCOMPARE(leechConnect.size(),16);
  QCOMPARE(qFromBigEndian<quint32>(seedConnect.constData()),quint32{0});
  QCOMPARE(qFromBigEndian<quint32>(seedConnect.constData()+4),quint32{0x10203040});
  const auto seedToken=qFromBigEndian<quint64>(seedConnect.constData()+8);
  const auto leechToken=qFromBigEndian<quint64>(leechConnect.constData()+8);
  QVERIFY(seedToken!=leechToken);

  const auto announcePacket=[&](quint64 token,char peer,quint16 listen,quint64 left) {
    auto request=udpPrefix(token,1,17)+QByteArray::fromStdString(area.hash)+QByteArray(20,peer);
    udpInteger(request,0,8);udpInteger(request,left,8);udpInteger(request,0,8);
    udpInteger(request,2,4);udpInteger(request,0,4);udpInteger(request,9,4);
    udpInteger(request,0xffffffff,4);udpInteger(request,listen,2);
    return request;
  };
  const auto seeded=announcePacket(seedToken,'s',5001,0);
  const auto wrongSource=exchangeUdp(area,*owner,foreign,port,seeded);
  QVERIFY(wrongSource.size()>=8);
  QCOMPARE(qFromBigEndian<quint32>(wrongSource.constData()),quint32{3});
  QCOMPARE(wrongSource.mid(8),QByteArray("Connection ID mismatch"));
  auto noSeeds=body(exchange(area,*owner,port,http("/scrape?info_hash="+encoded(area.hash))));
  QCOMPARE(noSeeds.find("files")->find(area.hash)->find("complete")->integer,std::int64_t{0});
  const auto seedReply=exchangeUdp(area,*owner,seeder,port,seeded);
  QCOMPARE(seedReply.size(),20);
  QCOMPARE(qFromBigEndian<quint32>(seedReply.constData()+16),quint32{1});
  const auto peerReply=exchangeUdp(area,*owner,leecher,port,announcePacket(leechToken,'l',5002,17));
  QCOMPARE(peerReply.size(),26);
  QCOMPARE(qFromBigEndian<quint32>(peerReply.constData()),quint32{1});
  QCOMPARE(qFromBigEndian<quint32>(peerReply.constData()+12),quint32{1});
  QCOMPARE(qFromBigEndian<quint32>(peerReply.constData()+16),quint32{1});
  QCOMPARE(qFromBigEndian<quint32>(peerReply.constData()+20),quint32{0x7f000001});
  QCOMPARE(qFromBigEndian<quint16>(peerReply.constData()+24),quint16{5001});
  const auto scrape=udpPrefix(leechToken,2,18)+QByteArray(20,'z')+QByteArray::fromStdString(area.hash);
  const auto scrapeReply=exchangeUdp(area,*owner,leecher,port,scrape);
  QCOMPARE(scrapeReply.size(),32);
  QCOMPARE(qFromBigEndian<quint32>(scrapeReply.constData()+8),quint32{0});
  QCOMPARE(qFromBigEndian<quint32>(scrapeReply.constData()+20),quint32{1});
  QCOMPARE(qFromBigEndian<quint32>(scrapeReply.constData()+28),quint32{1});
  catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&catalog->rows[0].runtime);
  QCOMPARE(catalog->rows[0].runtime->announces,std::int64_t{2});
  QVERIFY(catalog->rows[0].runtime->bytesOut>0);
  QVERIFY(owner->stopListener(*area.lease,501,{}));
  QVERIFY(exchangeUdp(area,*owner,seeder,port,connect,100).isEmpty());
  QVERIFY(owner->close().leaseReleased);
 }
 void importedTorrentPasswordProtectsUdpPackets() {
  Profile area;
  auto saved=storage::ConfigFile::load(*area.lease,"biglybt.config");
  const auto password=QByteArray("private udp fixture");
  const auto digest=QCryptographicHash::hash(password,QCryptographicHash::Sha1);
  QVERIFY(saved&&saved->document().replaceValue("Tracker Port UDP Enable","i1e"));
  QVERIFY(saved->document().replaceValue("Tracker Password Enable Torrent","i1e"));
  QVERIFY(saved->document().replaceValue("Tracker Username",b("Tester").toStdString()));
  QVERIFY(saved->document().replaceValue("Tracker Password",b(digest).toStdString()));
  QVERIFY(saved->save(*area.lease));
  auto owner=service(area);
  auto endpoint=startEndpoint(area,*owner);
  const auto port=endpoint.description().port;
  QCOMPARE(endpoint.description().udpAnnounceUrl,"udp://127.0.0.1:"+std::to_string(port)+"/announce?auth");
  auto catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&owner->start(*area.lease,501,catalog->rows[0].key,{}));

  QUdpSocket socket;
  QVERIFY(socket.bind(QHostAddress::LocalHost,0));
  const auto signedPacket=[](QByteArray data,const QByteArray& passwordDigest) {
    data.append("Tester\0\0",8);
    const auto signature=QCryptographicHash::hash(data+passwordDigest,QCryptographicHash::Sha1).left(8);
    return data+signature;
  };
  const auto isDenied=[](const QByteArray& reply) {
    return reply.size()>=8&&qFromBigEndian<quint32>(reply.constData())==3&&reply.mid(8)=="Access Denied";
  };
  const auto connect=udpPrefix(0x41727101980,0,42);
  QVERIFY(exchangeUdp(area,*owner,socket,port,connect,100).isEmpty());
  QVERIFY(isDenied(exchangeUdp(area,*owner,socket,port,signedPacket(connect,QByteArray(20,'x')))));
  const auto reply=exchangeUdp(area,*owner,socket,port,signedPacket(connect,digest));
  QCOMPARE(reply.size(),16);
  const auto token=qFromBigEndian<quint64>(reply.constData()+8);
  const auto scrape=udpPrefix(token,2,43)+QByteArray::fromStdString(area.hash);
  auto changed=signedPacket(scrape,digest);
  changed[15]=44;
  QVERIFY(isDenied(exchangeUdp(area,*owner,socket,port,changed)));
  catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&catalog->rows[0].runtime);
  QCOMPARE(catalog->rows[0].runtime->scrapes,std::int64_t{0});
  QCOMPARE(exchangeUdp(area,*owner,socket,port,signedPacket(scrape,digest)).size(),20);
  catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&catalog->rows[0].runtime);
  QCOMPARE(catalog->rows[0].runtime->scrapes,std::int64_t{1});
  QVERIFY(!bytes(area.path+"/biglybt.config").contains(password));
  QVERIFY(owner->close().leaseReleased);
 }
 void importedTorrentPasswordProtectsAnnounceAndScrape() {
  Profile area;
  const auto setup=storage::ConfigFile::load(*area.lease,"biglybt.config");
  QVERIFY(setup);
  auto saved=setup->document();
  const QByteArray password="private:fixture";
  const auto digest=QCryptographicHash::hash(password,QCryptographicHash::Sha1);
  QVERIFY(saved.replaceValue("Tracker Password Enable Torrent","i1e"));
  QVERIFY(saved.replaceValue("Tracker Username",b("Tester").toStdString()));
  QVERIFY(saved.replaceValue("Tracker Password",b(digest).toStdString()));
  write(area.path+"/biglybt.config",QByteArray::fromStdString(saved.serialize()));

  auto owner=service(area);
  auto endpoint=startEndpoint(area,*owner);
  auto catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&owner->start(*area.lease,501,catalog->rows[0].key,{}));
  const auto port=endpoint.description().port;
  const auto attempt=announce(area.hash,std::string(20,'p'),5555,"started",0);
  const auto scrape=http("/scrape?info_hash="+encoded(area.hash));
  const auto authorization=[](QByteArray request,const QByteArray& value) {
    request.insert(request.indexOf("\r\n")+2,"Authorization: "+value+"\r\n");
    return request;
  };
  const auto valid="bAsIc "+(QByteArray("tEsTeR:")+password).toBase64();
  const auto wrong="Basic "+QByteArray("Tester:incorrect").toBase64();
  const auto otherUser="Basic "+(QByteArray("Other:")+password).toBase64();
  const auto hashAsPassword="Basic "+(QByteArray("<internal>:")+digest.toBase64()).toBase64();
  const QList<QByteArray> rejected{attempt,scrape,authorization(attempt,wrong),authorization(attempt,otherUser),
      authorization(attempt,"Basic !!!"),authorization(attempt,"Bearer "+valid),
      authorization(authorization(attempt,valid),valid),authorization(attempt,hashAsPassword)};
  for (const auto& request:rejected) {
    const auto reply=exchange(area,*owner,port,request);
    QVERIFY(reply.startsWith("HTTP/1.1 401 Unauthorized\r\n"));
    QVERIFY(reply.contains("\r\nWWW-Authenticate: Basic realm=\"tracker\"\r\n"));
  }
  catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&catalog->rows[0].runtime);
  QCOMPARE(catalog->rows[0].runtime->announces,std::int64_t{0});
  QCOMPARE(catalog->rows[0].runtime->scrapes,std::int64_t{0});
  QCOMPARE(catalog->rows[0].runtime->bytesIn,std::int64_t{0});
  auto announced=body(exchange(area,*owner,port,authorization(attempt,valid)));
  QCOMPARE(announced.find("complete")->integer,std::int64_t{1});
  auto scraped=body(exchange(area,*owner,port,authorization(scrape,valid)));
  QCOMPARE(scraped.find("files")->find(area.hash)->find("complete")->integer,std::int64_t{1});
  catalog=owner->catalog(*area.lease,501);
  QVERIFY(catalog&&catalog->rows[0].runtime);
  QCOMPARE(catalog->rows[0].runtime->announces,std::int64_t{1});
  QCOMPARE(catalog->rows[0].runtime->scrapes,std::int64_t{1});
  QVERIFY(!bytes(area.path+"/biglybt.config").contains(password));
  QVERIFY(!bytes(area.path+"/tracker.config").contains(password));

  auto replacement=storage::ConfigFile::load(*area.lease,"biglybt.config");
  QVERIFY(replacement);
  QVERIFY(replacement->document().replaceValue("Tracker Password",b(QByteArray(20,'x')).toStdString()));
  QVERIFY(replacement->save(*area.lease));
  QVERIFY(!owner->poll(*area.lease,501));
  QVERIFY(!endpoint.current(*area.lease,501));
 }
 void malformedTorrentPasswordRefusesListener() {
  Profile area(false);
  auto owner=service(area);
  QVERIFY(owner->configure(*area.lease,501,{"127.0.0.1",0},{}));
  auto saved=storage::ConfigFile::load(*area.lease,"biglybt.config");
  QVERIFY(saved);
  QVERIFY(saved->document().replaceValue("Tracker Password Enable Torrent","i1e"));
  QVERIFY(saved->document().replaceValue("Tracker Password","2:no"));
  QVERIFY(saved->save(*area.lease));
  const auto original=bytes(area.path+"/biglybt.config");
  const auto started=owner->startListener(*area.lease,501,{});
  QVERIFY(!started);
  QCOMPARE(started.error().code,storage::ErrorCode::InvalidFormat);
  QCOMPARE(bytes(area.path+"/biglybt.config"),original);
  QVERIFY(owner->close().leaseReleased);
 }
 void sourceAndNamedLockRevocationHaveNoLateReply_data(){QTest::addColumn<bool>("lock");QTest::newRow("actual-policy-source")<<false;QTest::newRow("actual-named-lock-replaced")<<true;}
 void sourceAndNamedLockRevocationHaveNoLateReply(){
  QFETCH(bool,lock);Profile area;auto owner=service(area);auto endpoint=startEndpoint(area,*owner);auto catalog=owner->catalog(*area.lease,501);QVERIFY(catalog);QVERIFY(owner->start(*area.lease,501,catalog->rows[0].key,{}));const auto before=bytes(area.path+"/tracker.config");QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,endpoint.description().port);QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::ConnectedState,5000);QVERIFY(socket.write(announce(area.hash,std::string(20,'q'),4444))>0);socket.flush();QTest::qWait(20);
  if(lock){QVERIFY(QFile::rename(area.path+"/.azlock",area.path+"/original-lock"));write(area.path+"/.azlock","foreign-lock-bytes");}else{auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("Proxy.Data.Enable","i1e")&&config->save(*area.lease));}
  QVERIFY(!owner->poll(*area.lease,501));QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::UnconnectedState,5000);QVERIFY(socket.readAll().isEmpty());QVERIFY(!endpoint.current(*area.lease,501));QCOMPARE(bytes(area.path+"/tracker.config"),before);const auto closed=owner->close();QVERIFY(closed.listenerJoined&&closed.connectionsJoined&&closed.leaseReleased);area.lease.reset();if(lock){QCOMPARE(bytes(area.path+"/.azlock"),QByteArray("foreign-lock-bytes"));QVERIFY(QFile::remove(area.path+"/.azlock"));QVERIFY(QFile::rename(area.path+"/original-lock",area.path+"/.azlock"));}QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void indeterminateSettingsSaveStopsListenerAndRefusesBlindRetry(){
  Profile area(false);auto owner=service(area);QVERIFY(owner->configure(*area.lease,501,{"127.0.0.1",0},{}));syncCalls=0;failSync=3;auto endpoint=owner->startListener(*area.lease,501,{});failSync=0;QVERIFY(!endpoint);auto catalog=owner->catalog(*area.lease,501);QVERIFY(catalog&&catalog->needsReview&&!catalog->listener.running&&catalog->listener.joined);auto actual=document(bytes(area.path+"/biglybt.config"));QCOMPARE(actual.find("Tracker Port Enable")->integer,std::int64_t{1});QVERIFY(actual.find("Tracker Port")->integer>0);const auto before=bytes(area.path+"/biglybt.config");QVERIFY(!owner->startListener(*area.lease,501,{}));QCOMPARE(bytes(area.path+"/biglybt.config"),before);auto closed=owner->close();QVERIFY(closed.listenerJoined&&closed.leaseReleased);area.lease.reset();QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void unsupportedSettingsRefuseBeforeListener_data(){QTest::addColumn<QString>("key");for(const auto* key:{"Tracker Port SSL Enable","Tracker Password Enable Web","Tracker Public Enable","Tracker NAT Check Enable"}){QTest::newRow(key)<<QString::fromLatin1(key);}}
 void unsupportedSettingsRefuseBeforeListener(){QFETCH(QString,key);Profile area(false);auto owner=service(area);QVERIFY(owner->configure(*area.lease,501,{"127.0.0.1",0},{}));auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");QVERIFY(config&&config->document().replaceValue(key.toStdString(),"i1e")&&config->save(*area.lease));const auto before=bytes(area.path+"/biglybt.config");QVERIFY(!owner->startListener(*area.lease,501,{}));auto observed=owner->catalog(*area.lease,501);QVERIFY(observed&&!observed->listener.running&&observed->listener.joined);QCOMPARE(bytes(area.path+"/biglybt.config"),before);QVERIFY(owner->close().leaseReleased);}
 void partialConnectionStopJoinsAndReleasesAllAliases(){Profile area(false);auto owner=service(area);auto endpoint=startEndpoint(area,*owner);QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,endpoint.description().port);QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::ConnectedState,5000);QVERIFY(socket.write("GET /announce?info_hash=%00")>0);socket.flush();QVERIFY(owner->stopListener(*area.lease,501,{}));QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::UnconnectedState,5000);QVERIFY(socket.readAll().isEmpty());QVERIFY(!endpoint.current(*area.lease,501));const auto closed=owner->close();QVERIFY(closed.listenerJoined&&closed.connectionsJoined&&closed.leaseReleased);area.lease.reset();QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));}
};
QTEST_GUILESS_MAIN(EmbeddedTrackerTests)
#include "embedded_tracker_tests.moc"
