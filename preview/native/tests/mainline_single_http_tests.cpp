#include "library_http_policy.hpp"
#include "owned_mainline_krpc.hpp"
#include "library_commands.hpp"
#include "persistent_session.hpp"
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QtTest>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <sys/inotify.h>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace detail=engine::detail;
namespace wire=detail::http_tracker_wire;
namespace krpc=test::mainline;
std::int64_t millis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int64_t wallMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
core::TorrentHash hash(){return *core::TorrentHash::parse(std::string(20,'\x11'));}
QByteArray counts(const core::TorrentHash& key,int seeds=5,int peers=6){
  return krpc::dictionary({{"files",krpc::dictionary({{QByteArray(reinterpret_cast<const char*>(key.bytes().data()),20),krpc::dictionary({{"complete","i"+QByteArray::number(seeds)+"e"},{"downloaded","i9e"},{"incomplete","i"+QByteArray::number(peers)+"e"}})}})}});
}
engine::NetworkPolicy policy(){engine::NetworkPolicy p;p.mode=engine::NetworkMode::Normal;p.dht=true;p.httpTrackers=true;p.ipv6=false;p.bindAddresses={"127.0.0.1"};return p;}
QByteArray gzip(const QByteArray& raw){
  z_stream stream{};test::require(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"Cannot initialise owned gzip fixture");
  struct End{z_stream* s;~End(){deflateEnd(s);}} end{&stream};
  QByteArray encoded(static_cast<qsizetype>(compressBound(raw.size())+64),'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));stream.avail_in=static_cast<uInt>(raw.size());stream.next_out=reinterpret_cast<Bytef*>(encoded.data());stream.avail_out=static_cast<uInt>(encoded.size());
  test::require(deflate(&stream,Z_FINISH)==Z_STREAM_END,"Owned gzip fixture exceeded bounded output");encoded.resize(static_cast<qsizetype>(stream.total_out));return encoded;
}
struct Certificate {
  QSslCertificate certificate;QSslKey key;
  Certificate(){
    const auto freeKey=[](EVP_PKEY* p){EVP_PKEY_free(p);};const auto freeCert=[](X509* p){X509_free(p);};const auto freeBio=[](BIO* p){BIO_free(p);};
    std::unique_ptr<EVP_PKEY,decltype(freeKey)> privateKey(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime256v1"),freeKey);
    std::unique_ptr<X509,decltype(freeCert)> cert(X509_new(),freeCert);test::require(privateKey&&cert,"Cannot generate owned TLS identity");
    test::require(X509_set_version(cert.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1)==1
        &&X509_gmtime_adj(X509_getm_notBefore(cert.get()),-60)&&X509_gmtime_adj(X509_getm_notAfter(cert.get()),86400)
        &&X509_set_pubkey(cert.get(),privateKey.get())==1,"Cannot prepare owned TLS certificate");
    auto* subject=X509_get_subject_name(cert.get());test::require(X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("localhost"),-1,-1,0)==1&&X509_set_issuer_name(cert.get(),subject)==1,"Cannot set owned TLS subject");
    for(const auto& item:{std::pair{NID_subject_alt_name,"DNS:localhost"},std::pair{NID_basic_constraints,"critical,CA:TRUE"},std::pair{NID_key_usage,"critical,digitalSignature,keyCertSign"},std::pair{NID_ext_key_usage,"serverAuth"}}){
      auto* extension=X509V3_EXT_conf_nid(nullptr,nullptr,item.first,const_cast<char*>(item.second));test::require(extension&&X509_add_ext(cert.get(),extension,-1)==1,"Cannot add owned TLS extension");X509_EXTENSION_free(extension);
    }
    test::require(X509_sign(cert.get(),privateKey.get(),EVP_sha256())>0,"Cannot sign owned TLS certificate");
    std::unique_ptr<BIO,decltype(freeBio)> certBio(BIO_new(BIO_s_mem()),freeBio),keyBio(BIO_new(BIO_s_mem()),freeBio);
    test::require(certBio&&keyBio&&PEM_write_bio_X509(certBio.get(),cert.get())==1&&PEM_write_bio_PrivateKey(keyBio.get(),privateKey.get(),nullptr,nullptr,0,nullptr,nullptr)==1,"Cannot export owned TLS identity");
    char* certBytes=nullptr;char* keyBytes=nullptr;const auto certSize=BIO_get_mem_data(certBio.get(),&certBytes),keySize=BIO_get_mem_data(keyBio.get(),&keyBytes);
    certificate=QSslCertificate(QByteArray(certBytes,static_cast<qsizetype>(certSize)));key=QSslKey(QByteArray(keyBytes,static_cast<qsizetype>(keySize)),QSsl::Ec);
    test::require(!certificate.isNull()&&!key.isNull(),"Qt cannot load owned TLS identity");
  }
};
class HttpServer final:public QTcpServer {
public:
  QByteArray body=counts(hash()),lastRequest;QString failure;bool hold=false,chunked=false;
  int connections=0,requests=0;QHostAddress actualPeer;
  int redirectStatus=302;QByteArray location,encoding;
  QByteArray expectedHash=QByteArray(20,'\x11');
  const Certificate* tls=nullptr;
  QList<QPointer<QTcpSocket>> held;
  explicit HttpServer(const Certificate* c=nullptr):tls(c){test::require(listen(QHostAddress::LocalHost,0),"Cannot bind owned HTTP server");}
  std::string url(bool numeric=false)const{return std::string(tls?"https://":"http://")+(numeric?"127.0.0.1":"localhost")+":"+std::to_string(serverPort())+"/announce.php?pass=owned";}
  void release(){hold=false;const auto copy=held;held.clear();for(const auto& socket:copy){if(socket&&socket->state()==QAbstractSocket::ConnectedState){reply(socket);}}}
protected:
  void incomingConnection(qintptr descriptor)override{
    ++connections;QTcpSocket* socket=nullptr;
    if(tls){auto* ssl=new QSslSocket(this);ssl->setLocalCertificate(tls->certificate);ssl->setPrivateKey(tls->key);ssl->setPeerVerifyMode(QSslSocket::VerifyNone);socket=ssl;}
    else{socket=new QTcpSocket(this);}
    if(!socket->setSocketDescriptor(descriptor)){failure="Owned socket descriptor failed";socket->deleteLater();return;}actualPeer=socket->peerAddress();
    auto bytes=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this,socket,bytes,done]{*bytes+=socket->readAll();if(bytes->size()>8192){failure="Unbounded HTTP request";socket->abort();return;}if(*done||!bytes->contains("\r\n\r\n")){return;}*done=true;++requests;lastRequest=*bytes;
      const auto first=bytes->left(bytes->indexOf("\r\n"));const auto at=first.indexOf("info_hash=");const auto end=first.lastIndexOf(' ');
      if(at<0||end<=at+10||QByteArray::fromPercentEncoding(first.mid(at+10,end-at-10))!=expectedHash){failure="Actual HTTP info_hash did not match the selected saved hash";socket->abort();return;}
      if(hold){held.push_back(socket);}else{reply(socket);}});
    QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    if(tls){static_cast<QSslSocket*>(socket)->startServerEncryption();}
  }
private:
  void reply(QTcpSocket* socket){QByteArray encoded;
    const auto content=encoding.isEmpty()?QByteArray{}:"Content-Encoding: "+encoding+"\r\n";
    if(!location.isEmpty()){encoded="HTTP/1.1 "+QByteArray::number(redirectStatus)+" Redirect\r\nLocation: "+location+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";}
    else if(chunked){encoded="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"+content+"Connection: close\r\n\r\n"+QByteArray::number(body.size(),16)+"\r\n"+body+"\r\n0\r\n\r\n";}
    else{encoded="HTTP/1.1 200 OK\r\n"+content+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;}
    if(socket->write(encoded)!=encoded.size()){failure="Owned HTTP response write failed";}socket->disconnectFromHost();
  }
};
detail::LibraryHttpTracker::Request request(std::uint64_t nonce=1){return {hash(),"owned-http",std::string(64,'s'),"configuration",nonce};}
struct SavedBytes {
  QString path;QByteArray bytes;struct stat before{};
  explicit SavedBytes(QString p):path(std::move(p)),bytes(test::read(path)){test::require(::lstat(path.toStdString().c_str(),&before)==0,"Cannot snapshot owned control");}
  bool unchanged()const{struct stat now{};return ::lstat(path.toStdString().c_str(),&now)==0&&before.st_dev==now.st_dev&&before.st_ino==now.st_ino&&before.st_size==now.st_size&&before.st_nlink==now.st_nlink&&before.st_mode==now.st_mode&&before.st_mtim.tv_sec==now.st_mtim.tv_sec&&before.st_mtim.tv_nsec==now.st_mtim.tv_nsec&&before.st_ctim.tv_sec==now.st_ctim.tv_sec&&before.st_ctim.tv_nsec==now.st_ctim.tv_nsec&&test::read(path)==bytes;}
};
struct PayloadWatch {
  int fd=-1;
  explicit PayloadWatch(const test::OwnedEngineFixture& saved){fd=::inotify_init1(IN_NONBLOCK|IN_CLOEXEC);test::require(fd>=0,"Cannot create owned payload observer");
    constexpr auto mask=IN_OPEN|IN_ACCESS|IN_MODIFY|IN_ATTRIB|IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO;
    test::require(::inotify_add_watch(fd,saved.payload.toStdString().c_str(),mask)>=0,"Cannot watch owned payload root");
    for(const auto& f:saved.files){test::require(::inotify_add_watch(fd,(saved.payload+'/'+f.relative).toStdString().c_str(),mask)>=0,"Cannot watch owned payload file");}}
  ~PayloadWatch(){if(fd>=0){::close(fd);}}
  bool untouched()const{char data[4096];return ::read(fd,data,sizeof(data))<0&&errno==EAGAIN;}
  void stop(){if(fd>=0){::close(fd);fd=-1;}}
};
QByteArray originalCache(unsigned short port){
  const auto node=krpc::dictionary({{"addr",krpc::string(krpc::compact(port))},{"id",krpc::string(QByteArray(20,'K'))},{"created","i800e"},{"lastSeen","i995e"},{"failedCount","i0e"},{"verified","i1e"}});
  return krpc::dictionary({{"mainEntries",krpc::list({node})},{"replacements",krpc::list({})},{"oldKey",krpc::string(QByteArray(20,'R'))},{"timestamp","i1000e"},{"opaque",krpc::string(QByteArray::fromHex("00ff51"))}});
}
class MainlineSingleHttpTests final:public QObject {
  Q_OBJECT
private slots:
  void originalUrlMappingAndRoutePermissions(){
    auto p=policy();auto route=detail::libraryHttpRoute(p,"http://localhost:1234/path/announce.php?key=abc");QVERIFY(route);QCOMPARE(route->scrape.toEncoded(),QByteArray("http://localhost:1234/path/scrape.php?key=abc"));
    QVERIFY(detail::libraryHttpRoute(p,"https://example.org/"));QVERIFY(!detail::libraryHttpRoute(p,"http://localhost/file.php"));
    for(const auto* url:{"http://u:p@localhost/announce","http://localhost/announce#fragment","http://tracker.onion/announce","https://tracker.i2p/announce"}){QVERIFY(!detail::libraryHttpRoute(p,url));}
    QVERIFY(!detail::libraryHttpEndpoint(*route,{QHostAddress("10.1.2.3")}));p.bindAddresses={"0.0.0.0"};route=detail::libraryHttpRoute(p,"http://localhost/announce");QVERIFY(route);QVERIFY(!detail::libraryHttpEndpoint(*route,{QHostAddress::LocalHost}));
    p.proxy.kind=engine::NetworkProxyKind::Socks5;QVERIFY(!detail::libraryHttpRoute(p,"https://example.org/announce"));p.proxy.kind=engine::NetworkProxyKind::None;p.httpTrackers=false;QVERIFY(!detail::libraryHttpRoute(p,"https://example.org/announce"));
  }
  void sourceBodyMissingFailureNarrowingAndIntervals(){
    const auto key=hash();const std::span<const std::byte,20> bytes{key.bytes().data(),20};
    auto out=wire::scrape(counts(key).toStdString(),bytes,100,1000,900,false,17);QVERIFY(out.comparable);QCOMPARE(out.complete,std::int64_t{5});QCOMPARE(out.incomplete,std::int64_t{6});QCOMPARE(out.regularDelayMillis,std::optional<std::int64_t>{967000});
    // The parsed BT interval is retained, but a healthy regular response
    // suppresses this configured Mainline fallback even after that interval.
    core::QueueOriginalTrackerResponse response;response.requested();response.observe(out);QVERIFY(!response.regularDue(967099));QVERIFY(!response.regularDue(967100));
    auto missing=wire::scrape("d5:filesdee",bytes,200,1100,1000,false,0);QVERIFY(missing.definitiveFailure);QCOMPARE(missing.regularDelayMillis,std::optional<std::int64_t>{10800000});response.observe(missing);QCOMPARE(response.response().content->seeds,5);
    core::QueueOriginalTrackerResponse retry;retry.requested();retry.observe(missing);
    QVERIFY(!retry.regularDue(10800199));QVERIFY(retry.regularDue(10800200));
    auto failure=wire::scrape("d14:failure reason4:nopee",bytes,200,1100,1000,false,0);QVERIFY(failure.definitiveFailure);
    QVERIFY(!wire::scrape("d5:filesi1ee",bytes,200,1100,1000,false,0).definitiveFailure);
    auto defaults=wire::scrape(krpc::dictionary({{"files",krpc::dictionary({{QByteArray(reinterpret_cast<const char*>(key.bytes().data()),20),"de"}})}}).toStdString(),bytes,100,1000,900,false,0);QVERIFY(defaults.comparable);QCOMPARE(defaults.complete,std::int64_t{0});QCOMPARE(defaults.downloaded,std::int64_t{-1});
    auto overflow=wire::scrape(counts(key,-1,3).toStdString(),bytes,100,1000,900,false,0);QVERIFY(!overflow.comparable);QVERIFY(overflow.definitiveFailure);
    QCOMPARE(wire::interval(20000,1,179),std::int64_t{10800000});
    QVERIFY(wire::response("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nde",true).unsupported);
    QVERIFY(wire::response("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nde",true).unsupported);
  }
  void heldSettingsAreStrictAndLow32OriginalBooleans(){
    QTemporaryDir tmp;QVERIFY(tmp.isValid());auto lease=storage::ProfileLease::acquire(tmp.path().toStdString());QVERIFY(lease);QVERIFY(detail::libraryHttpConfiguration(*lease));
    test::write(tmp.path()+"/biglybt.config","d25:Tracker Client Enable TCPi0ee");QVERIFY(!detail::libraryHttpConfiguration(*lease));
    test::write(tmp.path()+"/biglybt.config","d25:Tracker Client Enable TCPi4294967297ee");QVERIFY(detail::libraryHttpConfiguration(*lease));
    test::write(tmp.path()+"/biglybt.config.saving","de");QVERIFY(!detail::libraryHttpConfiguration(*lease));
  }
  void actualHostnameHttpChunkedAndCancellationReuse(){
    HttpServer server;server.chunked=true;detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));std::optional<detail::LibraryHttpTracker::Result> actual;QString error;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!value){error=QString::fromStdString(value.error().message);return false;}if(*value){actual=std::move(**value);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(actual);QVERIFY(actual->terminal.comparable);QCOMPARE(actual->terminal.complete,std::int64_t{5});QCOMPARE(server.actualPeer,QHostAddress(QHostAddress::LocalHost));QVERIFY(server.lastRequest.startsWith("GET /scrape.php?pass=owned&info_hash="));QCOMPARE(server.requests,1);
    server.hold=true;actual.reset();QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||server.requests==2,10000);QVERIFY(error.isEmpty());client.cancel();QVERIFY(!client.active());server.release();
    QVERIFY(client.start(request(3),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(error.isEmpty());QVERIFY(actual);QCOMPARE(actual->request.nonce,std::uint64_t{3});QCOMPARE(server.requests,3);QVERIFY(actual->terminal.comparable);client.close();QVERIFY(server.failure.isEmpty());
  }
  void dnsProposalRequiresFreshOwnerPermissionBeforeSocket(){
    HttpServer server;detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QCOMPARE(server.connections,0);
    auto refused=client.poll(millis(),wallMillis(),[](const auto&){return false;});QVERIFY(refused);QVERIFY(!*refused);QVERIFY(!client.active());client.close();QCOMPARE(server.connections,0);
  }
  void neverAdmittedSavedHttpFailureUsesSameMainlineConsumerWithoutPayloadAccess(){
    test::OwnedEngineFixture saved(false);HttpServer tracker;tracker.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);tracker.body=krpc::dictionary({{"failure reason",krpc::string("owned unavailable")}});tracker.hold=true;
    auto metadata=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(metadata);QVERIFY(metadata->replaceValue("announce",test::encoded(libtorrent::entry(tracker.url()))));test::write(saved.stateFile,QByteArray::fromStdString(metadata->serialize()));
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));krpc::OwnedKrpc dht(peer.serverPort());const auto wanted=libtorrent::sha1_hash(reinterpret_cast<const char*>(saved.hash->bytes().data()));dht.allowWithoutPeers(wanted);
    const auto cache=saved.profile+"/plugins/mldht/IPv4-table.cache";test::write(cache,originalCache(dht.port()));
    const SavedBytes inventory(saved.profile+"/downloads.config"),state(saved.stateFile),statistics(saved.profile+"/BiglyBT.statistics"),tables(saved.profile+"/tables.config"),original(cache);
    const auto payload=test::read(saved.payload+"/sample.bin");auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);PayloadWatch watch(saved);
    auto p=policy();p.mainlineIpv4=true;p.mainlineIpv6=false;p.mainlinePeerBootstrapOnly=true;
    QTcpServer transfer,discovery;QVERIFY(transfer.listen(QHostAddress::LocalHost,0));QVERIFY(discovery.listen(QHostAddress::LocalHost,0));QUdpSocket reserved;QVERIFY(reserved.bind(QHostAddress::LocalHost,discovery.serverPort(),QUdpSocket::DontShareAddress));p.listenPort=transfer.serverPort();p.mainlinePort=discovery.serverPort();transfer.close();discovery.close();reserved.close();
    auto created=engine::PersistentSession::create({7961,7962,7963});QVERIFY(created);auto owner=std::move(*created);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QString failure;QElapsedTimer elapsed;elapsed.start();
    const auto pump=[&]{if(!owner->poll()){failure="HTTP saved owner poll failed";return false;}const auto current=owner->mainlineDiscovery();if(current.failure||current.queuedScrapeFailure){failure=QString::fromStdString(current.failure?current.failure->message:current.queuedScrapeFailure->message);return false;}if(!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()){failure="HTTP stats acquired a hidden torrent";return false;}if(!watch.untouched()){failure="HTTP stats accessed payload";return false;}failure=tracker.failure+dht.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||tracker.requests==1,25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QCOMPARE(dht.scrapes(),0);QVERIFY(owner->mainlineScrapeEstimates().empty());tracker.release();
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||!owner->mainlineScrapeEstimates().empty(),150000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(elapsed.elapsed()>=99999);QCOMPARE(tracker.requests,1);
    const auto estimates=owner->mainlineScrapeEstimates();QCOMPARE(estimates.size(),std::size_t{1});QCOMPARE(estimates[0].hash,*saved.hash);QCOMPARE(estimates[0].counts.responses,std::size_t{1});QCOMPARE(estimates[0].counts.seeds,1);QCOMPARE(estimates[0].counts.peers,1);QCOMPARE(dht.scrapes(),1);
    const auto current=owner->mainlineDiscovery();QCOMPARE(current.lookups,std::uint64_t{0});QCOMPARE(current.announces,std::uint64_t{0});QCOMPARE(current.deliveredPeers,std::uint64_t{0});QVERIFY(!peer.hasPendingConnections());
    QVERIFY(inventory.unchanged());QVERIFY(state.unchanged());QVERIFY(statistics.unchanged());QVERIFY(tables.unchanged());QVERIFY(original.unchanged());
    const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(watch.untouched());watch.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);QVERIFY(saved.intact(false));
  }
  void originalMinimumFailureAndSingleOnlyDialectAreSourceDerived(){
    const auto key=hash();const std::span<const std::byte,20> bytes{key.bytes().data(),20};
    const auto flags=krpc::dictionary({{"min_request_interval","i2000e"}});
    const auto files=krpc::dictionary({{QByteArray(reinterpret_cast<const char*>(key.bytes().data()),20),krpc::dictionary({{"complete","i1e"},{"incomplete","i2e"}})}});
    auto reply=wire::scrape(krpc::dictionary({{"files",files},{"flags",flags}}).toStdString(),bytes,100,1000,900,false,17);QVERIFY(reply.comparable);QCOMPARE(reply.regularDelayMillis,std::optional<std::int64_t>{2017000});
    auto failed=wire::scrape(krpc::dictionary({{"failure reason",krpc::string("unavailable")},{"flags",flags}}).toStdString(),bytes,100,1000,900,false,17);QVERIFY(failed.definitiveFailure);QCOMPARE(failed.regularDelayMillis,std::optional<std::int64_t>{2000000});
    const auto top=krpc::dictionary({{"complete","i5e"},{"downloaded","i-100e"},{"incomplete","i6e"},{"interval","i1200e"}});
    auto selected=wire::scrape(top.toStdString(),bytes,100,1000,900,true,0);QVERIFY(selected.comparable);QCOMPARE(selected.downloaded,std::int64_t{-1});QCOMPARE(selected.regularDelayMillis,std::optional<std::int64_t>{1200000});
    QVERIFY(!wire::scrape(top.toStdString(),bytes,100,1000,900,false,0).comparable);
    auto error=wire::response("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n",true);QVERIFY(error.complete&&error.failed&&!error.unsupported);
    QCOMPARE(wire::response("HTTP/1.1 302 Redirect\r\nLocation: https://elsewhere.invalid/\r\n\r\n",true).location,QByteArray("https://elsewhere.invalid/"));
  }
  void awaitingOwnerDeadlineCannotManufactureNetworkFailure(){
    HttpServer server;detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QCOMPARE(server.connections,0);
    // Real private-loop deadline, deliberately no owner approval or network
    // request. Bounded expiry is Unknown, never an invented RT_ERROR terminal.
    QTRY_VERIFY_WITH_TIMEOUT(!client.resolvedProposalPending(),35000);
    auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});QVERIFY(result);QVERIFY(*result);QVERIFY(!(**result).terminal.definitiveFailure);QVERIFY(!(**result).terminal.comparable);QCOMPARE(server.connections,0);client.close();
  }
  void gzipMemberBoundsCrcAndRedirectScopeAreExplicit(){
    const auto body=counts(hash());const auto one=gzip(body);const auto envelope=[](const QByteArray& encoded){return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: "+QByteArray::number(encoded.size())+"\r\n\r\n"+encoded;};
    auto decoded=wire::response(envelope(one),true);QVERIFY(decoded.complete&&!decoded.unsupported);QCOMPARE(decoded.bytes,body);
    const auto left=body.first(body.size()/2),right=body.mid(body.size()/2);decoded=wire::response(envelope(gzip(left)+gzip(right)),true);QVERIFY(decoded.complete);QCOMPARE(decoded.bytes,body);
    auto corrupt=one;corrupt[corrupt.size()-8]=static_cast<char>(corrupt[corrupt.size()-8]^1);QVERIFY(wire::response(envelope(corrupt),true).unsupported);
    QVERIFY(wire::response(envelope(one.first(one.size()-1)),true).unsupported);QVERIFY(wire::response(envelope(one+"garbage"),true).unsupported);
    QVERIFY(wire::response(envelope(gzip(QByteArray(128*1024+1,'x'))),true).unsupported);
    QByteArray members;for(int i=0;i<9;++i){members+=gzip({});}QVERIFY(wire::response(envelope(members),true).unsupported);
    auto original=detail::libraryHttpRoute(policy(),"https://localhost:1234/announce?key=owned");QVERIFY(original);const auto key=hash();const std::span<const std::byte,20> bytes{key.bytes().data(),20};
    QVERIFY(!detail::libraryHttpRedirectRoute(*original,"http://localhost:1234/moved",bytes));QVERIFY(!detail::libraryHttpRedirectRoute(*original,"https://user:password@localhost:1234/moved",bytes));QVERIFY(!detail::libraryHttpRedirectRoute(*original,"https://tracker.onion/moved",bytes));QVERIFY(!detail::libraryHttpRedirectRoute(*original,"https://tracker.onion./moved",bytes));QVERIFY(!detail::libraryHttpRoute(policy(),"https://tracker.i2p./announce"));QVERIFY(!detail::libraryHttpRedirectRoute(*original,"/moved?info_hash=wrong",bytes));
    auto relative=detail::libraryHttpRedirectRoute(*original,"/moved?key=kept",bytes);QVERIFY(relative);QCOMPARE(relative->scrape.toEncoded(),QByteArray("https://localhost:1234/moved?key=kept"));
    QVERIFY(wire::response("HTTP/1.1 305 Proxy\r\nLocation: http://localhost/\r\n\r\n",true).unsupported);QVERIFY(wire::response("HTTP/1.1 308 Newer\r\nLocation: http://localhost/\r\n\r\n",true).unsupported);
  }
  void actualSameHostRedirectRequiresFreshAuthorityAndDecodesGzip(){
    HttpServer first,second;first.location="http://localhost:"+QByteArray::number(second.serverPort())+"/moved?key=kept";second.encoding="gzip";second.body=gzip(counts(hash()));second.chunked=true;
    detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),first.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));std::optional<detail::LibraryHttpTracker::Result> actual;QString failure;int authorisations=0;
    const auto pump=[&]{auto result=client.poll(millis(),wallMillis(),[&](const auto&){++authorisations;return true;});if(!result){failure=QString::fromStdString(result.error().message);return false;}if(*result){actual=std::move(**result);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(actual);QVERIFY(actual->terminal.comparable);QCOMPARE(actual->terminal.complete,std::int64_t{5});QCOMPARE(first.requests,1);QCOMPARE(second.requests,1);QVERIFY(authorisations>=4);QVERIFY(second.lastRequest.startsWith("GET /moved?key=kept&info_hash="));QVERIFY(first.lastRequest.contains("Accept-Encoding: gzip"));QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());client.close();
  }
  void redirectedProposalRevocationAndLateCompressedReplyCannotGrant(){
    HttpServer first,second;first.location="http://localhost:"+QByteArray::number(second.serverPort())+"/moved";
    detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),first.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));std::optional<detail::LibraryHttpTracker::Result> actual;
    const auto pump=[&]{auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return false;}if(*result){actual=std::move(**result);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(client.redirectProposalPending()){return true;}return !pump()||client.redirectProposalPending();})(),10000);QVERIFY(client.redirectProposalPending());QCOMPARE(first.requests,1);QCOMPARE(second.connections,0);
    const auto refused=client.poll(millis(),wallMillis(),[](const auto&){return false;});QVERIFY(refused);QVERIFY(!*refused);QVERIFY(!client.active());
    first.location.clear();first.encoding="gzip";first.body=gzip(counts(hash()));first.hold=true;QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||first.requests==2,10000);QCOMPARE(first.requests,2);QVERIFY(!actual);client.cancel();first.release();
    QVERIFY(client.start(request(3),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(actual);QCOMPARE(actual->request.nonce,std::uint64_t{3});QVERIFY(actual->terminal.comparable);QCOMPARE(second.connections,0);QVERIFY(first.failure.isEmpty());client.close();
  }
  void actualGzipBombAndRedirectLoopAreUnavailableRatherThanError(){
    HttpServer server;server.encoding="gzip";server.body=gzip(QByteArray(128*1024+1,'x'));detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);std::optional<detail::LibraryHttpTracker::Result> actual;
    const auto pump=[&]{auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return false;}if(*result){actual=std::move(**result);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(actual);QVERIFY(!actual->terminal.comparable&&!actual->terminal.definitiveFailure);QCOMPARE(server.requests,1);
    actual.reset();server.encoding.clear();server.location="/scrape.php?pass=owned";QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),10000);QVERIFY(actual);QVERIFY(!actual->terminal.comparable&&!actual->terminal.definitiveFailure);QCOMPARE(server.requests,2);client.close();
  }
  void actualRedirectChainStopsBeforeSeventhRequest(){
    std::array<std::unique_ptr<HttpServer>,7> servers;for(auto& server:servers){server=std::make_unique<HttpServer>();}
    for(std::size_t i=0;i+1<servers.size();++i){servers[i]->location="http://localhost:"+QByteArray::number(servers[i+1]->serverPort())+"/hop-"+QByteArray::number(i+1);}
    detail::LibraryHttpTracker client;auto route=detail::libraryHttpRoute(policy(),servers[0]->url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));std::optional<detail::LibraryHttpTracker::Result> actual;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return true;}if(*result){actual=std::move(**result);}return actual.has_value();})(),15000);
    QVERIFY(actual);QVERIFY(!actual->terminal.comparable&&!actual->terminal.definitiveFailure);for(std::size_t i=0;i<6;++i){QCOMPARE(servers[i]->requests,1);QVERIFY(servers[i]->failure.isEmpty());}QCOMPARE(servers[6]->connections,0);client.close();
  }
  void redirectedTlsChecksTheNewHostnameBeforeSendingHash(){
    QVERIFY(QSslSocket::supportsSsl());Certificate identity;HttpServer first(&identity),second(&identity);first.location="https://127.0.0.1:"+QByteArray::number(second.serverPort())+"/moved";
    auto trust=QSslConfiguration::defaultConfiguration();trust.setCaCertificates({identity.certificate});trust.setProtocol(QSsl::TlsV1_2OrLater);
    detail::LibraryHttpTracker client;QVERIFY(client.testTlsConfiguration(trust));auto route=detail::libraryHttpRoute(policy(),first.url());QVERIFY(route);QVERIFY(client.start(request(),*route,millis(),wallMillis()));std::optional<detail::LibraryHttpTracker::Result> actual;
    const auto pump=[&]{auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return false;}if(*result){actual=std::move(**result);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),15000);QVERIFY(actual);QVERIFY(actual->terminal.definitiveFailure&&!actual->terminal.comparable);QCOMPARE(first.requests,1);QCOMPARE(second.requests,0);QCOMPARE(second.connections,1);
    actual.reset();first.location="https://localhost:"+QByteArray::number(second.serverPort())+"/moved";QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||actual.has_value(),15000);QVERIFY(actual);QVERIFY(actual->terminal.comparable);QCOMPARE(first.requests,2);QCOMPARE(second.requests,1);QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());client.close();
  }
  void actualTlsRequiresRootAndHostnameAndSupportsTrustedOwnedRoot(){
    QVERIFY(QSslSocket::supportsSsl());Certificate identity;HttpServer server(&identity);auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);
    const auto run=[&](detail::LibraryHttpTracker& client,std::uint64_t nonce,const detail::LibraryHttpRoute& selectedRoute,std::optional<detail::LibraryHttpTracker::Result>& out){
      if(!client.active()&&!out){if(!client.start(request(nonce),selectedRoute,millis(),wallMillis())){return false;}}
      auto result=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!result){return false;}if(*result){out=std::move(**result);}return true;
    };
    detail::LibraryHttpTracker untrusted;std::optional<detail::LibraryHttpTracker::Result> rejected;QTRY_VERIFY_WITH_TIMEOUT(!run(untrusted,1,*route,rejected)||rejected.has_value(),10000);QVERIFY(rejected);QVERIFY(rejected->terminal.definitiveFailure);QCOMPARE(server.requests,0);untrusted.close();
    auto trust=QSslConfiguration::defaultConfiguration();trust.setCaCertificates({identity.certificate});trust.setProtocol(QSsl::TlsV1_2OrLater);
    detail::LibraryHttpTracker trusted;QVERIFY(trusted.testTlsConfiguration(trust));std::optional<detail::LibraryHttpTracker::Result> accepted;QTRY_VERIFY_WITH_TIMEOUT(!run(trusted,2,*route,accepted)||accepted.has_value(),10000);QVERIFY(accepted);QVERIFY(accepted->terminal.comparable);QCOMPARE(server.requests,1);trusted.close();
    auto wrong=detail::libraryHttpRoute(policy(),server.url(true));QVERIFY(wrong);detail::LibraryHttpTracker mismatched;QVERIFY(mismatched.testTlsConfiguration(trust));std::optional<detail::LibraryHttpTracker::Result> denied;QTRY_VERIFY_WITH_TIMEOUT(!run(mismatched,3,*wrong,denied)||denied.has_value(),10000);QVERIFY(denied);QVERIFY(denied->terminal.definitiveFailure);QCOMPARE(server.requests,1);mismatched.close();
    const auto privateRoot=qEnvironmentVariable("BIGLYBTC_HTTP_PRIVATE_RUNTIME");
    if(!privateRoot.isEmpty()){
      QFile maps("/proc/self/maps");QVERIFY(maps.open(QIODevice::ReadOnly));const auto lines=maps.readAll().split('\n');
      for(const auto* name:{"libQt6Core.so.6","libQt6Network.so.6","libqopensslbackend.so"}){
        bool found=false;for(const auto& line:lines){if(!line.contains(name)){continue;}const auto path=line.mid(line.indexOf('/'));QVERIFY2(path.startsWith(privateRoot.toUtf8()+'/'),line.constData());found=true;}QVERIFY2(found,name);
      }
      QVERIFY2(QSslSocket::activeBackend()=="openssl",qPrintable(QSslSocket::activeBackend()));
    }
  }
};
}
int main(int argc,char** argv){
  QCoreApplication application(argc,argv);const auto privateRoot=qEnvironmentVariable("BIGLYBTC_HTTP_PRIVATE_RUNTIME");
  if(!privateRoot.isEmpty()){QCoreApplication::setLibraryPaths({privateRoot+"/qt/plugins"});}
  MainlineSingleHttpTests subject;return QTest::qExec(&subject,argc,argv);
}
#include "mainline_single_http_tests.moc"
