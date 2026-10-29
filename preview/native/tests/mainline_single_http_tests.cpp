#include "library_http_policy.hpp"
#include "managed_storage.hpp"
#include "managed_tracker_responses.hpp"
#include "persistent_runtime_guard.hpp"
#include "queued_mainline_scrape.hpp"
#include "owned_mainline_krpc.hpp"
#include "library_commands.hpp"
#include "persistent_session.hpp"
#include <QPointer>
#include <QTimer>
#include <QNetworkProxy>
#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
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
struct JksEntry {QByteArray alias;QList<QByteArray> certificates;bool key=false;};
QByteArray jksStore(const QList<JksEntry>& entries,quint32 version=2){
  QByteArray body;QDataStream stream(&body,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::BigEndian);
  const auto utf=[&](const QByteArray& value){stream<<static_cast<quint16>(value.size());test::require(stream.writeRawData(value.constData(),static_cast<int>(value.size()))==value.size(),"Cannot encode generated JKS string");};
  stream<<quint32{0xfeedfeed}<<version<<static_cast<quint32>(entries.size());
  for(const auto& entry:entries){stream<<quint32(entry.key?1:2);utf(entry.alias);stream<<quint64{1700000000000};
    if(entry.key){stream<<quint32{3};test::require(stream.writeRawData("key",3)==3,"Cannot encode opaque JKS key fixture");stream<<static_cast<quint32>(entry.certificates.size());}
    else{test::require(entry.certificates.size()==1,"Trusted JKS fixture requires one certificate");}
    for(const auto& cert:entry.certificates){if(version==2){utf("X.509");}stream<<static_cast<quint32>(cert.size());test::require(stream.writeRawData(cert.constData(),static_cast<int>(cert.size()))==cert.size(),"Cannot encode generated JKS certificate");}
  }
  // Independent fixture encoder: DataOutputStream layout and JavaKeyStore's
  // documented UTF-16BE password + whitening string + body SHA-1.
  return body+QCryptographicHash::hash(QByteArray::fromHex("006300680061006e0067006500690074")+"Mighty Aphrodite"+body,QCryptographicHash::Sha1);
}
class HttpServer final:public QTcpServer {
public:
  QByteArray body=counts(hash()),lastRequest;QString failure;bool hold=false,chunked=false;
  int connections=0,requests=0;QHostAddress actualPeer;
  int redirectStatus=302;QByteArray location,encoding;
  QByteArray expectedHash=QByteArray(20,'\x11');
  QByteArray basicRealm,requiredAuthorization;QList<QByteArray> authorizations;int challenges=0;
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
      QByteArray authorization;unsigned authFields=0;
      for(const auto& line:bytes->split('\n')){if(line.toLower().startsWith("authorization:")){++authFields;authorization=line.mid(14).trimmed();}}
      if(authFields>1){failure="Duplicate owned Authorization header";socket->abort();return;}authorizations.push_back(authorization);
      if(!basicRealm.isEmpty()&&authorization!=requiredAuthorization){++challenges;
        const auto denied="HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\""+basicRealm+"\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if(socket->write(denied)!=denied.size()){failure="Owned authentication challenge write failed";}socket->disconnectFromHost();return;}
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
// Controlled SOCKS5 endpoint: validates real greeting/auth/CONNECT bytes and
// tunnels only to its one known owned server. It never resolves target names.
class SocksProxy final:public QTcpServer {
  struct Client {QPointer<QTcpSocket> client,upstream;QByteArray input;int stage=0;};
  QList<std::weak_ptr<Client>> clients;
  const quint16 targetPort;
  void send(const std::shared_ptr<Client>& c,const QByteArray& data){if(c->client&&c->client->write(data)!=data.size()){failure="Owned SOCKS reply write failed";}}
  void greet(const std::shared_ptr<Client>& c){
    if(!c->client){return;}if(rejectMethod){send(c,QByteArray::fromHex("05ff"));c->client->disconnectFromHost();return;}
    send(c,authentication?QByteArray::fromHex("0502"):QByteArray::fromHex("0500"));c->stage=authentication?1:2;process(c);
  }
  void tunnel(const std::shared_ptr<Client>& c){
    if(!c->client||c->client->state()!=QAbstractSocket::ConnectedState){return;}
    if(malformedReply){send(c,QByteArray::fromHex("04000001000000000000"));c->client->disconnectFromHost();return;}
    if(rejectConnect){send(c,QByteArray::fromHex("05050001000000000000"));c->client->disconnectFromHost();return;}
    c->stage=3;auto* upstream=new QTcpSocket(this);c->upstream=upstream;upstream->setProxy(QNetworkProxy::NoProxy);upstream->setReadBufferSize(65536);
    QObject::connect(upstream,&QTcpSocket::connected,this,[this,c]{if(!c->client){if(c->upstream){c->upstream->abort();}return;}send(c,QByteArray::fromHex("05000001000000000000"));c->stage=4;process(c);});
    QObject::connect(upstream,&QTcpSocket::readyRead,this,[this,c]{if(c->upstream&&c->client){const auto bytes=c->upstream->readAll();if(bytes.size()>65536||c->client->write(bytes)!=bytes.size()){failure="Owned SOCKS upstream relay failed";}}});
    QObject::connect(upstream,&QTcpSocket::disconnected,this,[c]{if(c->client){c->client->disconnectFromHost();}});
    QObject::connect(upstream,&QTcpSocket::disconnected,upstream,&QObject::deleteLater);
    if(!upstream->bind(QHostAddress::LocalHost,0)){failure="Cannot bind owned SOCKS upstream";return;}upstream->connectToHost(QHostAddress::LocalHost,targetPort);
  }
  void process(const std::shared_ptr<Client>& c){
    if(!c->client){return;}if(c->stage==4){if(c->upstream&&!c->input.isEmpty()){if(c->upstream->write(c->input)!=c->input.size()){failure="Owned SOCKS client relay failed";}c->input.clear();}return;}
    const auto byte=[&](qsizetype i){return static_cast<unsigned char>(c->input[i]);};
    if(c->stage==0){if(c->input.size()<2){return;}const qsizetype needed=2+byte(1);if(c->input.size()<needed){return;}
      if(byte(0)!=5||!byte(1)||!c->input.mid(2,byte(1)).contains(authentication?char{2}:char{0})){failure="Invalid actual SOCKS greeting";return;}
      c->input.remove(0,needed);++greetings;c->stage=5;if(!holdGreeting){greet(c);}return;
    }
    if(c->stage==1){if(c->input.size()<2){return;}const qsizetype userBytes=byte(1);if(c->input.size()<3+userBytes){return;}const qsizetype passwordBytes=byte(2+userBytes),needed=3+userBytes+passwordBytes;if(c->input.size()<needed){return;}
      const auto user=c->input.mid(2,userBytes),password=c->input.mid(3+userBytes,passwordBytes);credentials.push_back({user,password});
      if(byte(0)!=1){failure="Invalid actual SOCKS auth version";return;}c->input.remove(0,needed);
      if(rejectAuthentication||user!="proxy-user"||password!="proxy-password"){send(c,QByteArray::fromHex("0101"));c->client->disconnectFromHost();return;}
      send(c,QByteArray::fromHex("0100"));c->stage=2;process(c);return;
    }
    if(c->stage!=2){return;}if(c->input.size()<4){return;}qsizetype needed=0;QString name;const auto type=byte(3);
    if(type==1){needed=10;if(c->input.size()<needed){return;}const auto ip=(quint32(byte(4))<<24)|(quint32(byte(5))<<16)|(quint32(byte(6))<<8)|byte(7);name=QHostAddress(ip).toString();}
    else if(type==4){needed=22;if(c->input.size()<needed){return;}Q_IPV6ADDR ip{};for(int i=0;i<16;++i){ip[i]=byte(4+i);}name=QHostAddress(ip).toString();}
    else if(type==3){if(c->input.size()<5){return;}needed=7+byte(4);if(c->input.size()<needed){return;}name=QString::fromLatin1(c->input.mid(5,byte(4)));}
    else{failure="Unexpected SOCKS target address type";return;}
    const auto port=quint16((unsigned(byte(needed-2))<<8)|byte(needed-1));
    if(byte(0)==5&&byte(1)==3&&byte(2)==0){++udpAssociates;c->input.remove(0,needed);send(c,QByteArray::fromHex("05070001000000000000"));c->client->disconnectFromHost();return;}
    if(byte(0)!=5||byte(1)!=1||byte(2)!=0||port!=targetPort
        ||(name!="127.0.0.1"&&name!="localhost"&&name!="owned-tracker.invalid")){failure="SOCKS CONNECT did not match the exact owned tracker";return;}
    ++connects;targetTypes.push_back(type);targetNames.push_back(name);targetPorts.push_back(port);c->input.remove(0,needed);c->stage=6;if(!holdConnect){tunnel(c);}
  }
protected:
  void incomingConnection(qintptr fd)override{
    ++connections;auto c=std::make_shared<Client>();auto* socket=new QTcpSocket(this);c->client=socket;socket->setReadBufferSize(65536);
    if(!socket->setSocketDescriptor(fd)){failure="Owned SOCKS descriptor failed";socket->deleteLater();return;}actualPeer=socket->peerAddress();clients.push_back(c);
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this,c]{if(!c->client){return;}const auto bytes=c->client->readAll();if(bytes.size()>65536||c->input.size()+bytes.size()>65536){failure="Unbounded owned SOCKS input";return;}c->input+=bytes;process(c);});
    QObject::connect(socket,&QTcpSocket::disconnected,this,[c]{if(c->upstream){c->upstream->abort();}});QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  }
public:
  bool authentication=false,holdGreeting=false,holdConnect=false,rejectMethod=false,rejectAuthentication=false,rejectConnect=false,malformedReply=false;
  int connections=0,greetings=0,connects=0,udpAssociates=0;QString failure;QHostAddress actualPeer;
  QList<unsigned> targetTypes;QList<QString> targetNames;QList<quint16> targetPorts;QList<QPair<QByteArray,QByteArray>> credentials;
  explicit SocksProxy(quint16 port):targetPort(port){test::require(listen(QHostAddress::LocalHost,0),"Cannot bind owned SOCKS proxy");}
  void release(){holdGreeting=false;holdConnect=false;const auto copy=clients;for(const auto& weak:copy){if(const auto c=weak.lock()){if(c->stage==5){greet(c);}else if(c->stage==6){tunnel(c);}}}}
};
engine::NetworkPolicy socksPolicy(const SocksProxy& proxy,bool remoteDns=true,bool authentication=false,bool proxyHostname=false){
  auto p=policy();p.proxy.kind=engine::NetworkProxyKind::Socks5;p.proxy.trackers=true;p.proxy.host=proxyHostname?"localhost":"127.0.0.1";p.proxy.port=proxy.serverPort();p.proxy.remoteDns=remoteDns;
  if(authentication){p.proxy.username="proxy-user";p.proxy.password="proxy-password";}return p;
}
QByteArray socksSettings(const engine::NetworkPolicy& p){
  return krpc::dictionary({{"Peer Source Selection Default.DHT",p.dht?"i1e":"i0e"},{"Enable.Proxy","i1e"},{"Enable.SOCKS","i1e"},{"Proxy.Host",krpc::string(QByteArray::fromStdString(p.proxy.host))},{"Proxy.Port",krpc::string(QByteArray::number(p.proxy.port))},
      {"Proxy.Username",krpc::string(QByteArray::fromStdString(p.proxy.username))},{"Proxy.Password",krpc::string(QByteArray::fromStdString(p.proxy.password))},{"Proxy.SOCKS.Tracker.DNS.Disable",p.proxy.remoteDns?"i1e":"i0e"}});
}
// Independent real announce/scrape endpoint for the admitted-owner path.
// It returns no peers and differentiates LT announces from the sole Qt scrape.
class AdmittedHttpServer final:public QTcpServer {
  const Certificate* tls;
public:
  QByteArray expectedHash;QString failure;int announces=0,scrapes=0;
  bool failedAnnounce=false,holdScrapes=false;int completedAnnounceConnections=0;QList<QPointer<QTcpSocket>> held;
  explicit AdmittedHttpServer(const Certificate* cert=nullptr):tls(cert){test::require(listen(QHostAddress::LocalHost,0),"Cannot bind owned admitted tracker");}
  std::string url()const{return std::string(tls?"https://":"http://")+"localhost:"+std::to_string(serverPort())+"/announce";}
  void release(){holdScrapes=false;const auto copy=held;held.clear();for(const auto& socket:copy){if(socket&&socket->state()==QAbstractSocket::ConnectedState){reply(socket,scrapeBody());}}}
private:
  QByteArray scrapeBody()const{return krpc::dictionary({{"files",krpc::dictionary({{expectedHash,krpc::dictionary({{"complete","i7e"},{"incomplete","i8e"},{"downloaded","i9e"}})}})}});}
  void reply(QTcpSocket* socket,const QByteArray& body,bool waitForClientClose=false){const auto bytes="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: "+(waitForClientClose?"keep-alive":"close")+"\r\n\r\n"+body;if(socket->write(bytes)!=bytes.size()){failure="Owned admitted reply failed";}if(!waitForClientClose){socket->disconnectFromHost();}}
protected:
  void incomingConnection(qintptr fd)override{
    QTcpSocket* socket=tls?static_cast<QTcpSocket*>(new QSslSocket(this)):new QTcpSocket(this);
    if(tls){auto* ssl=static_cast<QSslSocket*>(socket);ssl->setLocalCertificate(tls->certificate);ssl->setPrivateKey(tls->key);}
    if(!socket->setSocketDescriptor(fd)){failure="Owned admitted descriptor failed";socket->deleteLater();return;}
    auto input=std::make_shared<QByteArray>();auto complete=std::make_shared<bool>(false);
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this,socket,input,complete]{
      *input+=socket->readAll();if(input->size()>8192){failure="Owned admitted request bound";socket->abort();return;}if(*complete||!input->contains("\r\n\r\n")){return;}*complete=true;
      const auto line=input->left(input->indexOf("\r\n"));const auto parts=line.split(' ');if(parts.size()!=3||parts[0]!="GET"){failure="Owned admitted request line";return;}
      const auto path=parts[1].split('?');QByteArray actual;unsigned hashes=0;
      for(const auto& field:parts[1].mid(parts[1].indexOf('?')+1).split('&')){const auto equal=field.indexOf('=');if(field.left(equal)=="info_hash"){auto value=field.mid(equal+1);value.replace('+',' ');actual=QByteArray::fromPercentEncoding(value);++hashes;}}
      if(hashes!=1||actual!=expectedHash||path.isEmpty()){failure="Owned admitted selected hash mismatch";return;}
      if(path[0]=="/announce"){
        ++announces;if(!failedAnnounce){QObject::connect(socket,&QTcpSocket::disconnected,this,[this]{++completedAnnounceConnections;});}
        reply(socket,failedAnnounce?krpc::dictionary({{"failure reason",krpc::string("owned announce refusal")}}):krpc::dictionary({{"complete","i5e"},{"incomplete","i6e"},{"interval","i1800e"},{"peers",krpc::string({})}}),!failedAnnounce);
      }else if(path[0]=="/scrape"){++scrapes;if(holdScrapes){held.push_back(socket);}else{reply(socket,scrapeBody());}}
      else{failure="Unexpected owned admitted target path";}
    });
    QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    if(tls){static_cast<QSslSocket*>(socket)->startServerEncryption();}
  }
};
void prepareNormalAdmittedProxy(test::OwnedEngineFixture& saved,const std::string& url,bool privateTorrent){
  auto source=core::Document::parse(test::read(saved.stateFile).toStdString());test::require(source.has_value(),"Owned admitted source parse");
  test::require(source->replaceValue("announce",test::encoded(libtorrent::entry(url))).has_value(),"Owned admitted announce");
  if(privateTorrent){
    const auto raw=core::encodeValue(*source->find("info"));test::require(raw.has_value(),"Owned private info encode");auto info=core::Document::parse(*raw);test::require(info.has_value()&&info->replaceValue("private","i1e").has_value(),"Owned private flag");
    test::require(source->replaceValue("info",info->serialize()).has_value(),"Owned private info replace");const auto bytes=QCryptographicHash::hash(QByteArray::fromStdString(info->serialize()),QCryptographicHash::Sha1);
    const auto key=core::TorrentHash::parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));test::require(key.has_value(),"Owned private hash");saved.hash=*key;const auto old=saved.stateFile;saved.stateFile=saved.profile+"/active/"+QString::fromStdString(key->hex())+".dat";test::require(QFile::remove(old),"Owned old source remove");
  }
  const auto encoded=core::encodeValue(*source->find("attributes"));test::require(encoded.has_value(),"Owned attrs encode");auto attrs=core::Document::parse(*encoded);test::require(attrs.has_value(),"Owned attrs parse");
  test::require(attrs->replaceValue("peersources","l7:Trackere").has_value()&&attrs->replaceValue("peersourcesdenied","l3:DHTe").has_value()
      &&attrs->replaceValue("pluginoptions","d5:mldhtd14:enableannouncei1eee").has_value(),"Owned normal tracker permission");test::require(source->replaceValue("attributes",attrs->serialize()).has_value(),"Owned attrs replace");test::write(saved.stateFile,QByteArray::fromStdString(source->serialize()));
  auto inventory=core::Document::parse(test::read(saved.profile+"/downloads.config").toStdString());test::require(inventory.has_value(),"Owned inventory parse");const auto rowBytes=core::encodeValue(inventory->find("downloads")->children[0]);test::require(rowBytes.has_value(),"Owned row encode");auto row=core::Document::parse(*rowBytes);test::require(row.has_value(),"Owned row parse");
  const auto rawHash=std::string(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);
  test::require(row->replaceValue("torrent_hash",test::encoded(libtorrent::entry(rawHash))).has_value()&&row->replaceValue("torrent",test::encoded(libtorrent::entry(saved.stateFile.toStdString()))).has_value()&&row->replaceValue("state","i50e").has_value(),"Owned live intent fields");
  test::require(inventory->replaceValue("downloads","l"+row->serialize()+"e").has_value(),"Owned row replace");test::write(saved.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
}
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
QByteArray savedAuthentication(const QUrl& url,const QByteArray& realm,const QByteArray& user="owned-user",const QByteArray& password="owned-password"){
  const auto key=realm+':'+url.scheme().toUtf8()+"://"+url.host().toUtf8()+':'+QByteArray::number(url.port(url.scheme()=="https"?443:80))+"/";
  return krpc::dictionary({{"swt.auth.persistent.cache",krpc::dictionary({{key,krpc::dictionary({{"user",krpc::string(user)},{"pw",krpc::string(password)}})}})}});
}
class MainlineSingleHttpTests final:public QObject {
  Q_OBJECT
private slots:
  void actualAnnounceDefersWithoutReplacingRetainedContentOrShorteningScrapeDue(){
    core::QueueTrackerTerminal announce;announce.kind=core::QueueTrackerTerminalKind::Announce;announce.comparable=true;announce.present=3;announce.complete=5;announce.incomplete=6;announce.observedMillis=1000;announce.observedWallMillis=2000;
    const auto due=detail::libraryHttpAnnounceScrapeDeadline(announce);QVERIFY(due);QVERIFY(*due>=951000&&*due<=1130000);QCOMPARE(detail::libraryHttpAnnounceScrapeDeadline(announce),due);
    core::QueueOriginalTrackerResponse response;response.observe(announce);response.deferRegularUntil(*due);QVERIFY(!response.regularPollDue(*due-1));QVERIFY(response.regularPollDue(*due));QCOMPARE(response.response().content->seeds,5);
    response.deferRegularUntil(*due-100);QVERIFY(!response.regularPollDue(*due-1));response.requested();response.deferRegularUntil(*due+100);QVERIFY(!response.regularPollDue(*due+100));response.cancelRequest();QVERIFY(response.regularPollDue(*due+100));
    announce.kind=core::QueueTrackerTerminalKind::AnnounceFailed;announce.definitiveFailure=true;QVERIFY(!detail::libraryHttpAnnounceScrapeDeadline(announce));announce.kind=core::QueueTrackerTerminalKind::Announce;announce.complete=-1;QVERIFY(!detail::libraryHttpAnnounceScrapeDeadline(announce));
  }
  void chronologicalMergeRejectsOldCountStatusAndPreScrapeAnnounceDeadline(){
    auto response=std::make_shared<core::QueueOriginalTrackerResponse>();
    const auto deadline=+[](const core::QueueTrackerTerminal& value)->std::optional<std::int64_t>{if(value.kind!=core::QueueTrackerTerminalKind::Announce){return {};}return value.observedMillis+value.complete*1000;};
    detail::ManagedTrackerResponse ring(9,1,{},response,deadline);ring.installed=true;
    detail::ManagedTrackerResponse::Value old;old.admission=9;old.sequence=1;old.request=1;old.protocol=1;old.requestStartedNanos=1;old.observedNanos=1000000;
    old.terminal.kind=core::QueueTrackerTerminalKind::Announce;old.terminal.comparable=true;old.terminal.present=3;old.terminal.complete=10000;old.terminal.incomplete=6;old.terminal.observedMillis=1000;old.terminal.observedWallMillis=2000;
    ring.observe(old); // Actual older provider event is queued but not drained.
    core::QueueTrackerTerminal newer;newer.kind=core::QueueTrackerTerminalKind::Scrape;newer.comparable=true;newer.present=3;newer.complete=7;newer.incomplete=8;newer.observedMillis=2000;newer.observedWallMillis=3000;newer.regularDelayMillis=100;
    response->requested();QVERIFY(response->observeChronological(newer,2000000));QVERIFY(!response->observeChronological(newer,2000000));QVERIFY(ring.drain());QCOMPARE(response->response().content->seeds,7);QCOMPARE(response->response().content->peers,8);QVERIFY(!ring.latestTerminal());QVERIFY(!response->regularPollDue(2099));QVERIFY(response->regularPollDue(2100));
    auto staleError=newer;staleError.kind=core::QueueTrackerTerminalKind::ScrapeFailed;staleError.definitiveFailure=true;staleError.observedMillis=1500;QVERIFY(!response->observeChronological(staleError,1500000));QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Success);QVERIFY(response->regularPollDue(2100));
    // Multiple genuine announces after that scrape still keep their maximum
    // postponement even if the older one is drained after a later announce.
    auto later=old;later.sequence=2;later.request=2;later.observedNanos=4000000;later.terminal.complete=5;later.terminal.observedMillis=4000;ring.observe(later);QVERIFY(ring.drain());
    auto between=old.terminal;between.complete=100;between.observedMillis=3000;QVERIFY(!response->observeChronological(between,3000000));QVERIFY(response->announceTimingCurrent(3000000));response->deferRegularUntil(*deadline(between));QCOMPARE(response->response().content->seeds,5);QVERIFY(!response->regularPollDue(102999));QVERIFY(response->regularPollDue(103000));
  }
  void pendingQtScrapeCutsOldHeavyAnnounceButRetainsNewerAnnounceCountsAndTiming(){
    auto response=std::make_shared<core::QueueOriginalTrackerResponse>();
    const auto deadline=+[](const core::QueueTrackerTerminal& value)->std::optional<std::int64_t>{if(value.kind!=core::QueueTrackerTerminalKind::Announce){return {};}return value.observedMillis+value.complete*1000;};
    detail::ManagedTrackerResponse ring(9,1,{},response,deadline);ring.installed=true;response->requested();
    detail::ManagedTrackerResponse::Value first;first.admission=9;first.sequence=1;first.request=1;first.protocol=1;first.requestStartedNanos=1;first.observedNanos=1000000;
    first.terminal.kind=core::QueueTrackerTerminalKind::Announce;first.terminal.comparable=true;first.terminal.present=3;first.terminal.complete=10000;first.terminal.incomplete=6;first.terminal.observedMillis=1000;first.terminal.observedWallMillis=2000;
    auto third=first;third.sequence=2;third.request=2;third.observedNanos=3000000;third.terminal.complete=5;third.terminal.observedMillis=3000;third.terminal.observedWallMillis=4000;
    ring.observe(first);ring.observe(third);QVERIFY(ring.drain());QCOMPARE(response->response().content->seeds,5);
    core::QueueTrackerTerminal second;second.kind=core::QueueTrackerTerminalKind::Scrape;second.comparable=true;second.present=3;second.complete=7;second.incomplete=8;second.observedMillis=2000;second.observedWallMillis=3000;second.regularDelayMillis=100;
    QVERIFY(!response->observeChronological(second,2000000));response->cancelRequest();QCOMPARE(response->response().content->seeds,5);QCOMPARE(response->response().content->peers,6);QCOMPARE(response->response().content->startedWallMillis,std::int64_t{4000});
    QVERIFY(!response->regularPollDue(7999));QVERIFY(response->regularPollDue(8000)); // max(scrape2100,post-scrape announce8000), never pre-scrape10001000.
    // The ledger is per job, not a256-operation lifetime limit.
    for(int round=0;round<300;++round){response->requested();QVERIFY(response->deferRegularUntil(9000,4000000));response->cancelRequest();}
    response->requested();for(int i=0;i<256;++i){QVERIFY(response->deferRegularUntil(10000+i,5000000+i));}QVERIFY(!response->deferRegularUntil(20000,6000000));QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Unknown);QVERIFY(!response->regularPollDue(-1));
  }
  void providerScrapeDoesNotSettleTheSeparatePhysicalQtTimingWindow(){
    auto response=std::make_shared<core::QueueOriginalTrackerResponse>();
    const auto deadline=+[](const core::QueueTrackerTerminal& value)->std::optional<std::int64_t>{if(value.kind!=core::QueueTrackerTerminalKind::Announce){return {};}return value.observedMillis+value.complete*1000;};
    detail::ManagedTrackerResponse ring(9,1,{},response,deadline);ring.installed=true;response->requested();
    detail::ManagedTrackerResponse::Value first;first.admission=9;first.sequence=1;first.request=1;first.protocol=1;first.requestStartedNanos=1;first.observedNanos=1000000;first.terminal.kind=core::QueueTrackerTerminalKind::Announce;first.terminal.comparable=true;first.terminal.present=3;first.terminal.complete=10000;first.terminal.incomplete=6;first.terminal.observedMillis=1000;first.terminal.observedWallMillis=2000;
    auto second=first;second.sequence=2;second.request=2;second.observedNanos=2000000;second.terminal.kind=core::QueueTrackerTerminalKind::Scrape;second.terminal.complete=2;second.terminal.observedMillis=2000;second.terminal.regularDelayMillis=1000;
    auto fourth=first;fourth.sequence=3;fourth.request=3;fourth.observedNanos=4000000;fourth.terminal.complete=5;fourth.terminal.observedMillis=4000;fourth.terminal.observedWallMillis=5000;
    ring.observe(first);ring.observe(second);ring.observe(fourth);QVERIFY(ring.drain());QCOMPARE(response->response().content->seeds,5);
    auto third=second.terminal;third.complete=7;third.incomplete=8;third.observedMillis=3000;third.regularDelayMillis=100;
    QVERIFY(!response->observeChronological(third,3000000));response->cancelRequest();QCOMPARE(response->response().content->seeds,5);QCOMPARE(response->response().content->startedWallMillis,std::int64_t{5000});QVERIFY(!response->regularPollDue(8999));QVERIFY(response->regularPollDue(9000));
  }
  void announceDeadlinePreservesJavaOverflowWithCheckedSignedAddition(){
    core::QueueTrackerTerminal value;value.kind=core::QueueTrackerTerminalKind::Announce;value.comparable=true;value.present=3;value.complete=214748265;value.incomplete=1;value.observedMillis=0;
    const auto delay=wire::interval(0,214748265,98);QVERIFY(delay<0);const auto negative=wire::announceDeadline(value,98);QVERIFY(negative);QCOMPARE(*negative,delay);
    core::QueueOriginalTrackerResponse response;response.deferRegularUntil(5000);response.deferRegularUntil(*negative);QVERIFY(!response.regularPollDue(4999));QVERIFY(response.regularPollDue(5000));
    value.complete=5;value.observedMillis=INT64_MAX-1;QCOMPARE(wire::announceDeadline(value,0),std::optional<std::int64_t>{INT64_MAX});value.observedMillis=10;value.present=4;value.complete=-1;value.incomplete=-1;value.downloaded=-9;QVERIFY(wire::announceDeadline(value,0));value.present=0;QVERIFY(!wire::announceDeadline(value,0));
  }
  void everyActualAnnouncePostponesInOrderAndLostRingWithdrawsLatestResponse(){
    auto response=std::make_shared<core::QueueOriginalTrackerResponse>();
    const auto deadline=+[](const core::QueueTrackerTerminal& value)->std::optional<std::int64_t>{if(value.kind!=core::QueueTrackerTerminalKind::Announce){return {};}return value.observedMillis+value.complete*1000;};
    detail::ManagedTrackerResponse ring(9,1,{},response,deadline);ring.installed=true;
    detail::ManagedTrackerResponse::Value one;one.admission=9;one.sequence=1;one.request=1;one.protocol=1;one.requestStartedNanos=1;one.observedNanos=1000000;
    one.terminal.kind=core::QueueTrackerTerminalKind::Announce;one.terminal.comparable=true;one.terminal.present=3;one.terminal.complete=100;one.terminal.incomplete=6;one.terminal.observedMillis=1000;one.terminal.observedWallMillis=2000;
    auto two=one;two.sequence=2;two.request=2;two.observedNanos=2000000;two.terminal.complete=5;two.terminal.observedMillis=2000;two.terminal.observedWallMillis=3000;
    ring.observe(one);ring.observe(two);QVERIFY(ring.drain());QCOMPARE(response->response().content->seeds,5);QVERIFY(!response->regularPollDue(100999));QVERIFY(response->regularPollDue(101000));QVERIFY(ring.latestTerminal());QCOMPARE(ring.latestTerminal()->sequence,std::uint64_t{2});
    auto failed=two;failed.sequence=3;failed.request=3;failed.observedNanos=3000000;failed.terminal.kind=core::QueueTrackerTerminalKind::AnnounceFailed;failed.terminal.definitiveFailure=true;ring.observe(failed);QVERIFY(ring.drain());QCOMPARE(ring.latestTerminal()->sequence,std::uint64_t{2});QCOMPARE(response->response().content->seeds,5);QVERIFY(!response->regularPollDue(100999));
    auto wrong=two;wrong.sequence=4;wrong.request=4;wrong.observedNanos=4000000;wrong.tracker[0]=1;ring.observe(wrong);QVERIFY(!ring.drain());QVERIFY(!ring.latestTerminal());QCOMPARE(response->response().type,core::QueueOriginalScrapeType::Unknown);
  }
  void admittedProxyReaderRequiresActualCapturedUrlFullSubjectAndFreshTrackerPermission(){
    test::OwnedEngineFixture saved(false);HttpServer server;SocksProxy proxy(server.serverPort());const auto selected=socksPolicy(proxy);prepareNormalAdmittedProxy(saved,server.url(),false);
    auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);auto storage=detail::ManagedStorage::prepare(*lease,*saved.hash,selected);QVERIFY(storage);engine::ManagedMainlineScrapeObservation observation{&*storage,8831,false};
    const auto read=[&]{return engine::readCurrentMainlineScrapeTargets(*lease,std::span(&observation,1),true,nullptr,engine::SavedTrackerPurpose::RegularHttp);};
    auto normal=read();QVERIFY(normal);QCOMPARE(normal->size(),std::size_t{1});QCOMPARE(normal->front().admission,std::uint64_t{8831});QCOMPARE(normal->front().configuredTracker,server.url());auto old=engine::readCurrentMainlineScrapeTargets(*lease,std::span(&observation,1),true);QVERIFY(old&&old->empty());
    const auto original=test::read(saved.stateFile);auto changed=core::Document::parse(original.toStdString());QVERIFY(changed);QVERIFY(changed->replaceValue("announce",test::encoded(libtorrent::entry("http://localhost:1/announce"))));test::write(saved.stateFile,QByteArray::fromStdString(changed->serialize()));auto different=read();QVERIFY(different&&different->empty());test::write(saved.stateFile,original);
    auto denied=core::Document::parse(original.toStdString());QVERIFY(denied);auto raw=core::encodeValue(*denied->find("attributes"));QVERIFY(raw);auto attrs=core::Document::parse(*raw);QVERIFY(attrs);QVERIFY(attrs->replaceValue("peersourcesdenied","l7:Trackere"));QVERIFY(denied->replaceValue("attributes",attrs->serialize()));test::write(saved.stateFile,QByteArray::fromStdString(denied->serialize()));auto noPermission=read();QVERIFY(noPermission&&noPermission->empty());test::write(saved.stateFile,original);
  }
  void normalAdmittedProxyUsesActualAnnounceOrSoleScrapeAndWithdrawsBeforeLateDelivery_data(){
    QTest::addColumn<int>("mode");QTest::newRow("HTTP-healthy-announce-no-redundant-scrape")<<0;QTest::newRow("HTTP-failed-announce-real-scrape")<<1;QTest::newRow("private-HTTPS-saved-JKS-real-scrape")<<2;QTest::newRow("private-HTTP-stop-before-held-scrape")<<3;
  }
  void normalAdmittedProxyUsesActualAnnounceOrSoleScrapeAndWithdrawsBeforeLateDelivery(){
    QFETCH(int,mode);QVERIFY(detail::managedTrackerResponseRuntimePin());const bool encrypted=mode==2,privateTorrent=mode>=2;Certificate identity;AdmittedHttpServer server(encrypted?&identity:nullptr);server.failedAnnounce=mode!=0;server.holdScrapes=mode==3;
    test::OwnedEngineFixture saved(false);SocksProxy proxy(server.serverPort());auto selected=socksPolicy(proxy);selected.dht=privateTorrent;QTcpServer listen;QVERIFY(listen.listen(QHostAddress::LocalHost,0));selected.listenPort=listen.serverPort();listen.close();prepareNormalAdmittedProxy(saved,server.url(),privateTorrent);server.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);test::write(saved.profile+"/biglybt.config",socksSettings(selected));
    if(encrypted){QVERIFY(QSslSocket::supportsSsl());test::write(saved.profile+"/.certs",jksStore({{"owned",{identity.certificate.toDer()},false}}));}
    const auto rawBefore=test::read(saved.stateFile);const auto doc=core::Document::parse(rawBefore.toStdString());QVERIFY(doc);const auto rawInfo=core::encodeValue(*doc->find("info"));QVERIFY(rawInfo);const auto payload=test::read(saved.payload+"/sample.bin");
    auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);auto created=engine::PersistentSession::create({8841,8842,8843});QVERIFY(created);auto owner=std::move(*created);QVERIFY(owner->configureNetwork(selected));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QVERIFY(owner->startManaged(*lease,{.hash=*saved.hash,.queued=false}));QString failure;
    if(mode==0){
      // No owner pump runs before this actual first-announce barrier. The
      // owned server leaves the reply socket open; LT on_response dispatches
      // its callback BEFORE close(), so client closure proves the real ring
      // terminal was emitted before the first regular-producer worker turn.
      QTRY_VERIFY_WITH_TIMEOUT(server.completedAnnounceConnections>0,15000);QVERIFY(server.failure.isEmpty());QCOMPARE(server.scrapes,0);
    }
    const auto pump=[&]{if(!owner->poll()){failure="Normal admitted proxy poll failed";return false;}for(const auto& value:owner->managedDownloads()){if(value.failure){failure=QString::fromStdString(value.failure->message);return false;}}failure=server.failure+proxy.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return true;}const auto rows=owner->managedDownloads();return rows.size()==1&&rows[0].checked&&rows[0].retained&&rows[0].phase==engine::ManagedPhase::Downloading;}()),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto admission=owner->managedDownloads()[0].admission;QVERIFY(admission);QVERIFY(!owner->managedTrackerResponse(*saved.hash,admission+1));
    if(mode==3){
      QTRY_VERIFY_WITH_TIMEOUT(!pump()||server.scrapes==1,15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(owner->pauseManaged(*saved.hash,true));QVERIFY(owner->libraryTrackerResponses().empty());server.release();QVERIFY(pump());const auto late=owner->managedTrackerResponse(*saved.hash,admission);QVERIFY(!late||late->type!=core::QueueOriginalScrapeType::Success);QVERIFY(owner->libraryTrackerResponses().empty());
    }else{
      QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!pump()){return true;}const auto views=owner->libraryTrackerResponses();return views.size()==1&&views[0].admission==admission&&views[0].response.content&&views[0].response.content->seeds==(mode==0?5:7)&&views[0].response.content->peers==(mode==0?6:8);}()),20000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto views=owner->libraryTrackerResponses();QCOMPARE(views.size(),std::size_t{1});QCOMPARE(views[0].hash,*saved.hash);QCOMPARE(views[0].admission,admission);QCOMPARE(views[0].response.type,core::QueueOriginalScrapeType::Success);const auto actual=owner->managedTrackerResponse(*saved.hash,admission);QVERIFY(actual&&actual->content);QCOMPARE(actual->content->seeds,mode==0?5:7);QCOMPARE(server.scrapes,mode==0?0:1);if(!encrypted){QVERIFY(server.announces>=1);}QVERIFY(owner->pauseManaged(*saved.hash,true));QVERIFY(owner->libraryTrackerResponses().empty());
    }
    const auto discovery=owner->mainlineDiscovery();QCOMPARE(discovery.lookups,std::uint64_t{0});QCOMPARE(discovery.announces,std::uint64_t{0});QCOMPARE(discovery.deliveredPeers,std::uint64_t{0});QVERIFY(owner->mainlineScrapeEstimates().empty());
    const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(owner->libraryTrackerResponses().empty());QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);const auto after=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(after);const auto afterInfo=core::encodeValue(*after->find("info"));QVERIFY(afterInfo);QCOMPARE(*afterInfo,*rawInfo);QVERIFY(saved.accountingIntact(false));QVERIFY(server.failure.isEmpty()&&proxy.failure.isEmpty());
  }
  void socks5RoutesKeepExplicitProxyDnsAndSavedConfigurationAuthority(){
    HttpServer server;SocksProxy proxy(server.serverPort());auto p=socksPolicy(proxy);p.dht=false;
    auto route=detail::libraryHttpRoute(p,server.url());QVERIFY(route&&route->socks5);QVERIFY(route->socks5->remoteDns);
    auto numeric=detail::libraryHttpProxyEndpoint(*route,{QHostAddress::LocalHost});QVERIFY(numeric);QCOMPARE(numeric->first,QHostAddress(QHostAddress::LocalHost));
    QVERIFY(!detail::libraryHttpProxyEndpoint(*route,{QHostAddress::AnyIPv4}));QVERIFY(!detail::libraryHttpProxyEndpoint(*route,{QHostAddress("224.1.2.3")}));
    for(const auto* target:{"http://hidden.onion/announce","https://hidden.i2p/announce"}){QVERIFY(!detail::libraryHttpRoute(p,target));}
    for(const auto kind:{engine::NetworkProxyKind::Http,engine::NetworkProxyKind::Socks4,engine::NetworkProxyKind::Socks4a}){auto other=p;other.proxy.kind=kind;QVERIFY(!detail::libraryHttpRoute(other,server.url()));}
    auto unselected=p;unselected.proxy.trackers=false;QVERIFY(!detail::libraryHttpRoute(unselected,server.url()));
    QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);const auto file=root.filePath("biglybt.config");test::write(file,socksSettings(p));
    auto config=detail::libraryHttpConfiguration(*lease,false,nullptr,&p);QVERIFY(config);const auto source=config->scope;
    auto changed=p;changed.proxy.password="changed-secret";changed.proxy.username="proxy-user";test::write(file,socksSettings(changed));
    QVERIFY(!detail::libraryHttpConfiguration(*lease,false,nullptr,&p));auto accepted=detail::libraryHttpConfiguration(*lease,false,nullptr,&changed);QVERIFY(accepted);QVERIFY(accepted->scope!=source);QVERIFY(accepted->scope.find("changed-secret")==std::string::npos);
    auto changedRoute=detail::libraryHttpRoute(changed,server.url());QVERIFY(changedRoute);QVERIFY(detail::libraryHttpRouteScope(*changedRoute,accepted->scope).find("changed-secret")==std::string::npos);
  }
  void actualSocks5RemoteDnsKeepsTlsOriginAndTrackerBasicSeparate_data(){
    QTest::addColumn<bool>("encrypted");QTest::addColumn<bool>("proxyAuthentication");
    QTest::newRow("http-no-auth")<<false<<false;QTest::newRow("http-proxy-userpass")<<false<<true;
    QTest::newRow("https-no-auth")<<true<<false;QTest::newRow("https-proxy-userpass")<<true<<true;
  }
  void actualSocks5RemoteDnsKeepsTlsOriginAndTrackerBasicSeparate(){
    QFETCH(bool,encrypted);QFETCH(bool,proxyAuthentication);Certificate identity;HttpServer server(encrypted?&identity:nullptr);SocksProxy proxy(server.serverPort());proxy.authentication=proxyAuthentication;
    auto p=socksPolicy(proxy,true,proxyAuthentication,!encrypted);auto url=QUrl(QString::fromStdString(server.url()));if(!encrypted){url.setHost("owned-tracker.invalid");}
    auto route=detail::libraryHttpRoute(p,url.toEncoded().toStdString());QVERIFY(route);
    server.basicRealm="tracker-owned";server.requiredAuthorization="Basic "+QByteArray("tracker-user:tracker-password").toBase64();auto cache=core::Document::parse(savedAuthentication(route->scrape,server.basicRealm,"tracker-user","tracker-password").toStdString());QVERIFY(cache);
    detail::LibraryHttpTracker client;if(encrypted){QVERIFY(QSslSocket::supportsSsl());auto trust=QSslConfiguration::defaultConfiguration();trust.setCaCertificates({identity.certificate});QVERIFY(client.testTlsConfiguration(trust));}
    std::optional<detail::LibraryHttpTracker::Result> result;QString failure;int approvals=0;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[&](const auto&){++approvals;return true;},[&](const auto&,const auto& actual,const auto& realm){return detail::libraryHttpSavedBasic(&*cache,actual,realm);});if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result&&result->terminal.comparable);QCOMPARE(result->terminal.complete,std::int64_t{5});QCOMPARE(result->terminal.incomplete,std::int64_t{6});QVERIFY(approvals>=4);
    QCOMPARE(proxy.connects,2);QCOMPARE(server.requests,2);QCOMPARE(proxy.actualPeer,QHostAddress(QHostAddress::LocalHost));QCOMPARE(proxy.targetNames,(QList<QString>{route->scrape.host(),route->scrape.host()}));
    QCOMPARE(proxy.targetTypes,(QList<unsigned>{3,3}));QCOMPARE(proxy.targetPorts,(QList<quint16>{server.serverPort(),server.serverPort()}));
    const auto submitted=client.resolverSubmissions();if(encrypted){QVERIFY(submitted.empty());}else{QCOMPARE(submitted,std::vector<std::string>({"localhost","localhost"}));}
    QVERIFY(server.authorizations[0].isEmpty());QCOMPARE(server.authorizations[1],server.requiredAuthorization);QVERIFY(!server.lastRequest.contains("proxy-user"));QVERIFY(!server.lastRequest.contains("Proxy-Authorization"));
    if(proxyAuthentication){QCOMPARE(proxy.credentials.size(),qsizetype{2});for(const auto& credential:proxy.credentials){QCOMPARE(credential.first,QByteArray("proxy-user"));QCOMPARE(credential.second,QByteArray("proxy-password"));}}
    else{QVERIFY(proxy.credentials.isEmpty());}QVERIFY2(proxy.failure.isEmpty(),qPrintable(proxy.failure));QVERIFY(server.failure.isEmpty());client.close();
  }
  void actualSocks5LocalDnsUsesNumericConnectAndCancellationCannotReuseOldReply(){
    HttpServer server;SocksProxy proxy(server.serverPort());auto p=socksPolicy(proxy,false);auto route=detail::libraryHttpRoute(p,server.url());QVERIFY(route);detail::LibraryHttpTracker client;
    std::optional<detail::LibraryHttpTracker::Result> result;QString failure;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),10000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result&&result->terminal.comparable);QCOMPARE(proxy.targetTypes,QList<unsigned>{1});QCOMPARE(proxy.targetNames,QList<QString>{"127.0.0.1"});QCOMPARE(client.resolverSubmissions(),std::vector<std::string>{"localhost"});
    result.reset();proxy.holdConnect=true;QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||proxy.connects==2,10000);QVERIFY(failure.isEmpty());client.cancel();QVERIFY(!client.active());
    QVERIFY(client.start(request(3),*route,millis(),wallMillis()));proxy.release();QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),10000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result&&result->terminal.comparable);QCOMPARE(result->request.nonce,std::uint64_t{3});QCOMPARE(server.requests,2);QCOMPARE(proxy.connects,3);QVERIFY(proxy.failure.isEmpty()&&server.failure.isEmpty());client.close();
  }
  void injectedTrackerLookupHostNotFoundUsesFreshSameProxyDomainProposal(){
    // Inject only the resolver outcome; the proxy/TCP/HTTP flow below is real
    // owned loopback. This does not claim an actual external DNS failure.
    HttpServer server;SocksProxy proxy(server.serverPort());auto p=socksPolicy(proxy,false);auto url=QUrl(QString::fromStdString(server.url()));url.setHost("owned-tracker.invalid");
    auto route=detail::libraryHttpRoute(p,url.toEncoded().toStdString());QVERIFY(route);
    detail::LibraryHttpResolver resolver;
    resolver.submit=[](const QString& host,QObject* context,detail::LibraryHttpResolver::Completion completion){
      QHostInfo result;result.setHostName(host);result.setError(QHostInfo::HostNotFound);
      QTimer::singleShot(0,context,[completion=std::move(completion),result]{completion(result);});return 1;
    };
    resolver.cancel=[](int){};detail::LibraryHttpTracker client(std::move(resolver));std::optional<detail::LibraryHttpTracker::Result> result;
    QVERIFY(!detail::libraryHttpProxyTarget(*route,{}));const auto unresolved=detail::libraryHttpProxyTarget(*route,{},true);QVERIFY(unresolved&&unresolved->isNull());
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);
    QCOMPARE(proxy.connections,0);QCOMPARE(server.connections,0);QCOMPARE(client.resolverSubmissions(),std::vector<std::string>{"owned-tracker.invalid"});
    int approvals=0;const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[&](const auto&){++approvals;return true;});if(!value){return false;}if(*value){result=std::move(**value);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),10000);QVERIFY(result&&result->terminal.comparable);QCOMPARE(result->terminal.complete,std::int64_t{5});QCOMPARE(result->terminal.incomplete,std::int64_t{6});QVERIFY(approvals>=2);
    QCOMPARE(proxy.connects,1);QCOMPARE(proxy.targetTypes,QList<unsigned>{3});QCOMPARE(proxy.targetNames,QList<QString>{"owned-tracker.invalid"});QCOMPARE(server.requests,1);QVERIFY(proxy.failure.isEmpty());client.close();
    // The same queued fallback has no authority if its owner now refuses it.
    detail::LibraryHttpResolver revokedResolver;
    revokedResolver.submit=[](const QString& host,QObject* context,detail::LibraryHttpResolver::Completion completion){QHostInfo value;value.setHostName(host);value.setError(QHostInfo::HostNotFound);QTimer::singleShot(0,context,[completion=std::move(completion),value]{completion(value);});return 1;};revokedResolver.cancel=[](int){};
    detail::LibraryHttpTracker revoked(std::move(revokedResolver));QVERIFY(revoked.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(revoked.resolvedProposalPending(),10000);
    auto rejected=revoked.poll(millis(),wallMillis(),[](const auto&){return false;});QVERIFY(rejected&&!*rejected);QVERIFY(!revoked.active());QCOMPARE(proxy.connects,1);QCOMPARE(server.requests,1);revoked.close();
    detail::LibraryHttpResolver unavailable;
    unavailable.submit=[](const QString& host,QObject* context,detail::LibraryHttpResolver::Completion completion){QHostInfo value;value.setHostName(host);value.setError(QHostInfo::UnknownError);QTimer::singleShot(0,context,[completion=std::move(completion),value]{completion(value);});return 1;};unavailable.cancel=[](int){};
    detail::LibraryHttpTracker unknown(std::move(unavailable));result.reset();QVERIFY(unknown.start(request(3),*route,millis(),wallMillis()));
    const auto unknownPump=[&]{auto value=unknown.poll(millis(),wallMillis(),[](const auto&){return true;});if(!value){return false;}if(*value){result=std::move(**value);}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!unknownPump()||result.has_value(),10000);QVERIFY(result&&!result->terminal.comparable&&!result->terminal.definitiveFailure);QCOMPARE(proxy.connects,1);QCOMPARE(server.requests,1);unknown.close();
  }
  void actualSocks5FailuresNeverRetryDirectAndMalformedReplyStaysUnknown_data(){
    QTest::addColumn<int>("mode");QTest::newRow("method-refused")<<0;QTest::newRow("auth-refused")<<1;QTest::newRow("connect-refused")<<2;QTest::newRow("malformed-socks-version")<<3;
  }
  void actualSocks5FailuresNeverRetryDirectAndMalformedReplyStaysUnknown(){
    QFETCH(int,mode);HttpServer server;SocksProxy proxy(server.serverPort());proxy.authentication=mode==1;proxy.rejectMethod=mode==0;proxy.rejectAuthentication=mode==1;proxy.rejectConnect=mode==2;proxy.malformedReply=mode==3;
    auto p=socksPolicy(proxy,true,mode==1);auto route=detail::libraryHttpRoute(p,server.url());QVERIFY(route);detail::LibraryHttpTracker client;std::optional<detail::LibraryHttpTracker::Result> result;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[](const auto&){return true;});if(!value){return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),10000);QVERIFY(result);QVERIFY(!result->terminal.comparable);QCOMPARE(result->terminal.definitiveFailure,mode!=3);QCOMPARE(server.connections,0);QCOMPARE(server.requests,0);QCOMPARE(proxy.connections,1);QVERIFY(proxy.failure.isEmpty());client.close();
  }
  void savedProxyChangesWithdrawResolvedEndpointBeforeAnyConnection(){
    HttpServer server;SocksProxy proxy(server.serverPort());auto p=socksPolicy(proxy,true,true,true);auto route=detail::libraryHttpRoute(p,server.url());QVERIFY(route);
    QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);const auto path=root.filePath("biglybt.config");test::write(path,socksSettings(p));
    auto config=detail::libraryHttpConfiguration(*lease,false,nullptr,&p);QVERIFY(config);auto current=request();current.configuration=detail::libraryHttpRouteScope(*route,config->scope);detail::LibraryHttpTracker client;QVERIFY(client.start(current,*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QCOMPARE(proxy.connections,0);QCOMPARE(client.resolverSubmissions(),std::vector<std::string>{"localhost"});
    auto changed=p;changed.proxy.port=p.proxy.port==65535?65534:p.proxy.port+1;test::write(path,socksSettings(changed));
    auto rejected=client.poll(millis(),wallMillis(),[&](const auto& request){auto fresh=detail::libraryHttpConfiguration(*lease,false,nullptr,&p);return fresh&&request.configuration==detail::libraryHttpRouteScope(*route,fresh->scope);});QVERIFY(rejected&&!*rejected);QVERIFY(!client.active());QCOMPARE(proxy.connections,0);QCOMPARE(server.connections,0);client.close();
  }
  void genuinePrivateOrDhtDisabledSavedOwnerConsumesProxyResponseWithoutDiscovery_data(){
    QTest::addColumn<bool>("privateTorrent");QTest::newRow("public-DHT-disabled")<<false;QTest::newRow("private-Tracker-only")<<true;
  }
  void genuinePrivateOrDhtDisabledSavedOwnerConsumesProxyResponseWithoutDiscovery(){
    QFETCH(bool,privateTorrent);test::OwnedEngineFixture saved(false);HttpServer server;SocksProxy proxy(server.serverPort());auto p=socksPolicy(proxy);p.dht=privateTorrent;
    QTcpServer transfer;QVERIFY(transfer.listen(QHostAddress::LocalHost,0));p.listenPort=transfer.serverPort();transfer.close();
    auto source=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(source);QVERIFY(source->replaceValue("announce",test::encoded(libtorrent::entry(server.url()))));
    if(privateTorrent){
      auto bytes=core::encodeValue(*source->find("info"));QVERIFY(bytes);auto info=core::Document::parse(*bytes);QVERIFY(info);QVERIFY(info->replaceValue("private","i1e"));QVERIFY(source->replaceValue("info",info->serialize()));
      const auto raw=QCryptographicHash::hash(QByteArray::fromStdString(info->serialize()),QCryptographicHash::Sha1);auto key=core::TorrentHash::parse(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));QVERIFY(key);saved.hash=*key;
      const auto old=saved.stateFile;saved.stateFile=saved.profile+"/active/"+QString::fromStdString(key->hex())+".dat";QVERIFY(QFile::remove(old));
      auto inventory=core::Document::parse(test::read(saved.profile+"/downloads.config").toStdString());QVERIFY(inventory);auto rowBytes=core::encodeValue(inventory->find("downloads")->children[0]);QVERIFY(rowBytes);auto row=core::Document::parse(*rowBytes);QVERIFY(row);QVERIFY(row->replaceValue("torrent_hash",test::encoded(libtorrent::entry(raw.toStdString()))));QVERIFY(row->replaceValue("torrent",test::encoded(libtorrent::entry(saved.stateFile.toStdString()))));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(saved.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
    }
    auto attrsBytes=core::encodeValue(*source->find("attributes"));QVERIFY(attrsBytes);auto attrs=core::Document::parse(*attrsBytes);QVERIFY(attrs);QVERIFY(attrs->replaceValue("peersources","l7:Trackere"));QVERIFY(attrs->replaceValue("peersourcesdenied","l3:DHTe"));QVERIFY(attrs->replaceValue("flags","i512e"));QVERIFY(source->replaceValue("attributes",attrs->serialize()));test::write(saved.stateFile,QByteArray::fromStdString(source->serialize()));
    server.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);server.body=counts(*saved.hash);test::write(saved.profile+"/biglybt.config",socksSettings(p));
    const SavedBytes state(saved.stateFile),statistics(saved.profile+"/BiglyBT.statistics"),config(saved.profile+"/biglybt.config");const auto payload=test::read(saved.payload+"/sample.bin");auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);PayloadWatch watch(saved);
    auto created=engine::PersistentSession::create({7971,7972,7973});QVERIFY(created);auto owner=std::move(*created);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QString failure;
    const auto pump=[&]{if(!owner->poll()){failure="SOCKS regular owner poll failed";return false;}if(!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()||!watch.untouched()){failure="Saved tracker proxy acquired payload authority";return false;}failure=server.failure+proxy.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||!owner->libraryTrackerResponses().empty(),25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));const auto responses=owner->libraryTrackerResponses();QCOMPARE(responses.size(),std::size_t{1});QCOMPARE(responses[0].hash,*saved.hash);QCOMPARE(responses[0].response.type,core::QueueOriginalScrapeType::Success);QVERIFY(responses[0].response.content);QCOMPARE(responses[0].response.content->seeds,5);QCOMPARE(responses[0].response.content->peers,6);QVERIFY(responses[0].observedWallMillis>0);QCOMPARE(server.requests,1);QCOMPARE(proxy.connects,1);
    auto discovery=owner->mainlineDiscovery();QCOMPARE(discovery.state,privateTorrent?engine::MainlineDiscoveryStatus::State::Unavailable:engine::MainlineDiscoveryStatus::State::Disabled);QCOMPARE(discovery.lookups,std::uint64_t{0});QCOMPARE(discovery.announces,std::uint64_t{0});QCOMPARE(discovery.deliveredPeers,std::uint64_t{0});QVERIFY(owner->mainlineScrapeEstimates().empty());QVERIFY(!QFileInfo::exists(saved.profile+"/plugins/mldht"));
    owner->revokeMainlineScrape(*saved.hash);QVERIFY(owner->libraryTrackerResponses().empty());QVERIFY(storage::applyLibraryCommand(*lease,*saved.hash,core::DownloadAction::Stop));QVERIFY(owner->stopManaged(*saved.hash));QVERIFY(owner->rearmMainlineScrape(*saved.hash));QVERIFY(pump());QVERIFY(owner->libraryTrackerResponses().empty());
    const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(owner->libraryTrackerResponses().empty());QVERIFY(state.unchanged()&&statistics.unchanged()&&config.unchanged());QVERIFY(watch.untouched());watch.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);QVERIFY(saved.intact(false));
  }
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
  void savedTrackerTiersBindAllUrlsAndPreservePrimaryFallbackAndRawIdentity(){
    test::OwnedEngineFixture fixture(false);auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    const auto state=test::read(fixture.stateFile),inventory=test::read(fixture.profile+"/downloads.config"),payload=test::read(fixture.payload+"/sample.bin");
    const std::string a="http://localhost:12001/announce",b="udp://127.0.0.1:12002/announce",c="https://localhost:12003/announce";
    const auto write=[&](const std::string& primary,const libtorrent::entry::list_type& tiers){
      auto doc=core::Document::parse(state.toStdString());if(!doc||!doc->replaceValue("announce",test::encoded(libtorrent::entry(primary)))||!doc->replaceValue("announce-list",test::encoded(libtorrent::entry(tiers)))){return false;}
      test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));return true;
    };
    const auto tier=[](std::initializer_list<std::string> urls){libtorrent::entry::list_type list;for(const auto& url:urls){list.emplace_back(url);}return libtorrent::entry(list);};
    QVERIFY(write(c,{tier({a,b})}));auto found=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(found);QCOMPARE(found->size(),std::size_t{1});
    const auto original=found->front();QCOMPARE(original.hash,*fixture.hash);QCOMPARE(original.configuredTrackers.size(),std::size_t{3});
    QCOMPARE(original.configuredTrackers[0].url,a);QCOMPARE(original.configuredTrackers[1].url,b);QCOMPARE(original.configuredTrackers[2].url,c);
    QCOMPARE(original.configuredTrackers[0].tier,0u);QCOMPARE(original.configuredTrackers[1].tier,0u);QCOMPARE(original.configuredTrackers[2].tier,1u);
    auto selected=engine::selectConfiguredMainlineTracker(original,original.configuredTrackers[1].responseKey);QVERIFY(selected);QCOMPARE(selected->configuredTracker,b);QVERIFY(selected->scope!=original.scope);
    QVERIFY(!engine::selectConfiguredMainlineTracker(original,"unknown"));
    QVERIFY(write(a,{tier({a}),tier({b})}));auto changed=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(changed&&changed->size()==1);QCOMPARE(changed->front().configuredTrackers.size(),std::size_t{2});QVERIFY(changed->front().scope!=original.scope);
    auto member=engine::selectConfiguredMainlineTracker(changed->front(),original.configuredTrackers[1].responseKey);QVERIFY(member);QVERIFY(member->scope!=selected->scope); // SAME URL cannot borrow a previous tier/source scope.
    QVERIFY(write(a,{tier({a,a})}));auto duplicate=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(duplicate&&duplicate->size()==1);QVERIFY(duplicate->front().configuredTrackers.empty());
    libtorrent::entry::list_type repeated;for(unsigned i=0;i<9;++i){repeated.push_back(tier({a}));}QVERIFY(write(a,repeated));auto legacy=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(legacy&&legacy->size()==1&&legacy->front().configuredTrackers.empty());
    libtorrent::entry::list_type excessive;for(unsigned i=0;i<9;++i){excessive.push_back(tier({"http://localhost:"+std::to_string(12100+i)+"/announce"}));}
    QVERIFY(write(a,excessive));auto bounded=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(bounded&&bounded->empty());
    QVERIFY(write(a,{tier({a}),tier({"wss://localhost/announce"})}));auto unsupported=engine::readCurrentMainlineScrapeTargets(*lease,{},true);QVERIFY(unsupported&&unsupported->empty());
    QVERIFY(write(a,{tier({a}),tier({b})}));auto doc=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(doc);auto attrs=core::encodeValue(*doc->find("attributes"));QVERIFY(attrs);auto denied=core::Document::parse(*attrs);QVERIFY(denied);QVERIFY(denied->replaceValue("peersourcesdenied","l7:Trackere"));QVERIFY(doc->replaceValue("attributes",denied->serialize()));test::write(fixture.stateFile,QByteArray::fromStdString(doc->serialize()));std::vector<std::string> allRows;auto revoked=engine::readCurrentMainlineScrapeTargets(*lease,{},true,&allRows);QVERIFY(revoked&&revoked->empty());QCOMPARE(allRows,std::vector<std::string>{fixture.hash->hex()}); // Eligibility loss does not end the manager lifetime.
    auto before=core::Document::parse(state.toStdString()),after=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(before&&after);auto oldInfo=storage::originalTorrentInfoSubject(*before),newInfo=storage::originalTorrentInfoSubject(*after);QVERIFY(oldInfo&&newInfo);QCOMPARE(*oldInfo,*newInfo);
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);test::write(fixture.stateFile,state);QVERIFY(fixture.intact(false));
  }
  void savedTrackerTiersFailOverToActualSecondHttpAndStopRevokesHeldReply_data(){
    QTest::addColumn<bool>("sameTier");QTest::newRow("ordered-tiers")<<false;QTest::newRow("stable-shuffled-tier")<<true;
  }
  void savedTrackerTiersFailOverToActualSecondHttpAndStopRevokesHeldReply(){
    QFETCH(bool,sameTier);
    test::OwnedEngineFixture saved(false);HttpServer first,second,third;
    for(auto* tracker:{&first,&second,&third}){tracker->expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);tracker->body=counts(*saved.hash);}
    first.body=krpc::dictionary({{"failure reason",krpc::string("owned first unavailable")}});second.body=first.body;first.hold=true;second.hold=true;
    auto metadata=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(metadata);
    const auto tier=[](const std::string& url){return libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(url)});};
    QVERIFY(metadata->replaceValue("announce",test::encoded(libtorrent::entry(first.url()))));
    const auto tiers=sameTier?libtorrent::entry::list_type{libtorrent::entry(libtorrent::entry::list_type{libtorrent::entry(first.url()),libtorrent::entry(second.url())}),tier(third.url())}
        :libtorrent::entry::list_type{tier(first.url()),tier(second.url()),tier(third.url())};
    QVERIFY(metadata->replaceValue("announce-list",test::encoded(libtorrent::entry(tiers))));test::write(saved.stateFile,QByteArray::fromStdString(metadata->serialize()));
    const SavedBytes state(saved.stateFile),statistics(saved.profile+"/BiglyBT.statistics"),tables(saved.profile+"/tables.config");const auto payload=test::read(saved.payload+"/sample.bin");
    auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);PayloadWatch watch(saved);auto p=policy();p.mainlineIpv4=true;p.mainlineIpv6=false;p.mainlinePeerBootstrapOnly=true;
    QTcpServer transfer,discovery;QVERIFY(transfer.listen(QHostAddress::LocalHost,0));QVERIFY(discovery.listen(QHostAddress::LocalHost,0));QUdpSocket reserved;QVERIFY(reserved.bind(QHostAddress::LocalHost,discovery.serverPort(),QUdpSocket::DontShareAddress));p.listenPort=transfer.serverPort();p.mainlinePort=discovery.serverPort();transfer.close();discovery.close();reserved.close();
    auto created=engine::PersistentSession::create({7964,7965,7966});QVERIFY(created);auto owner=std::move(*created);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QString failure;
    const auto pump=[&]{if(!owner->poll()){failure="Multi-tracker saved owner poll failed";return false;}const auto current=owner->mainlineDiscovery();if(current.failure||current.queuedScrapeFailure){failure=QString::fromStdString(current.failure?current.failure->message:current.queuedScrapeFailure->message);return false;}if(!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()||!watch.untouched()){failure="Multi-tracker stats acquired or accessed payload";return false;}failure=first.failure+second.failure+third.failure;return failure.isEmpty();};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||first.requests+second.requests==1,25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));
    auto* initial=first.requests?&first:&second;auto* next=first.requests?&second:&first;
    if(!sameTier){QCOMPARE(initial,&first);}QCOMPARE(next->connections,0);QCOMPARE(third.connections,0);QVERIFY(owner->mainlineScrapeEstimates().empty());
    next->body=counts(*saved.hash);initial->release();
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||next->requests==1,25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QCOMPARE(initial->requests,1);QCOMPARE(third.connections,0);QVERIFY(owner->mainlineScrapeEstimates().empty());
    // The real first error only authorises visiting the next tracker. It is
    // not an all-configured-trackers-failed response or DHT permission.
    owner->revokeMainlineScrape(*saved.hash);auto stopped=storage::applyLibraryCommand(*lease,*saved.hash,core::DownloadAction::Stop);QVERIFY(stopped);QVERIFY(owner->stopManaged(*saved.hash));QVERIFY(owner->rearmMainlineScrape(*saved.hash));
    next->release();QVERIFY(pump());QVERIFY(owner->mainlineScrapeEstimates().empty());QCOMPARE(third.connections,0);
    const auto current=owner->mainlineDiscovery();QCOMPARE(current.lookups,std::uint64_t{0});QCOMPARE(current.announces,std::uint64_t{0});QCOMPARE(current.deliveredPeers,std::uint64_t{0});
    const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(state.unchanged()&&statistics.unchanged()&&tables.unchanged());QVERIFY(watch.untouched());watch.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),payload);QVERIFY(saved.intact(false));
  }
  void savedBasicChallengeAndCacheAreExactRealmOriginAndSecretFreeScope(){
    const auto envelope=[](const QByteArray& value){return "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: "+value+"\r\nContent-Length: 0\r\n\r\n";};
    auto challenge=wire::response(envelope("Basic realm=\"owned\", charset=\"UTF-8\""),false);QVERIFY(challenge.complete&&challenge.basicAuthentication);QCOMPARE(challenge.basicRealm,QByteArray("owned"));
    for(const auto& field:{QByteArray("Basic realm=\"a\", realm=\"b\""),QByteArray("Digest realm=\"owned\""),QByteArray("Basic realm=\"unfinished"),QByteArray("B"),QByteArray("Basic realm=\"a\\b\""),QByteArray("Basic realm=unquoted text")}){QVERIFY(wire::response(envelope(field),false).unsupported);}
    QVERIFY(wire::response("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"a\"\r\nWWW-Authenticate: Basic realm=\"b\"\r\n\r\n",false).unsupported);
    auto route=detail::libraryHttpRoute(policy(),"http://localhost:23456/announce");QVERIFY(route);
    auto doc=core::Document::parse(savedAuthentication(route->scrape,"owned").toStdString());QVERIFY(doc);
    auto auth=detail::libraryHttpSavedBasic(&*doc,*route,"owned");QVERIFY(auth&&*auth);QCOMPARE(**auth,"Basic "+QByteArray("owned-user:owned-password").toBase64());
    auth=detail::libraryHttpSavedBasic(&*doc,*route,"other");QVERIFY(auth&&!*auth);
    auto other=detail::libraryHttpRoute(policy(),"http://localhost:23457/announce");QVERIFY(other);auth=detail::libraryHttpSavedBasic(&*doc,*other,"owned");QVERIFY(auth&&!*auth);
    auto numeric=detail::libraryHttpRoute(policy(),"http://127.0.0.1:23456/announce");QVERIFY(numeric);QVERIFY(!detail::libraryHttpSavedBasic(&*doc,*numeric,"owned"));
    auto invalid=core::Document::parse(savedAuthentication(route->scrape,"owned","name:ambiguous").toStdString());QVERIFY(invalid);QVERIFY(!detail::libraryHttpSavedBasic(&*invalid,*route,"owned"));
    QTemporaryDir dir;QVERIFY(dir.isValid());auto lease=storage::ProfileLease::acquire(dir.path().toStdString());QVERIFY(lease);test::write(dir.filePath("biglybt.config"),QByteArray::fromStdString(doc->serialize()));
    auto old=detail::libraryHttpConfiguration(*lease);QVERIFY(old);QVERIFY(old->scope.find("owned-password")==std::string::npos);
    test::write(dir.filePath("biglybt.config"),savedAuthentication(route->scrape,"owned","owned-user","changed"));auto fresh=detail::libraryHttpConfiguration(*lease);QVERIFY(fresh);QVERIFY(fresh->scope!=old->scope);
  }
  void actualSavedBasicIsChallengedBeforeCredentialsAndUsesHttpOrTls_data(){
    QTest::addColumn<bool>("encrypted");QTest::newRow("http")<<false;QTest::newRow("verified-https")<<true;
  }
  void actualSavedBasicIsChallengedBeforeCredentialsAndUsesHttpOrTls(){
    QFETCH(bool,encrypted);Certificate certificate;HttpServer server(encrypted?&certificate:nullptr);server.basicRealm="owned";server.requiredAuthorization="Basic "+QByteArray("owned-user:owned-password").toBase64();
    detail::LibraryHttpTracker client;if(encrypted){QVERIFY(QSslSocket::supportsSsl());auto trust=QSslConfiguration::defaultConfiguration();trust.setCaCertificates({certificate.certificate});QVERIFY(client.testTlsConfiguration(trust));}
    auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);auto doc=core::Document::parse(savedAuthentication(route->scrape,server.basicRealm).toStdString());QVERIFY(doc);
    std::optional<detail::LibraryHttpTracker::Result> result;QString failure;int credentialReads=0,approvals=0;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[&](const auto&){++approvals;return true;},[&](const auto&,const auto& actual,const auto& realm){++credentialReads;return detail::libraryHttpSavedBasic(&*doc,actual,realm);});if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result&&result->terminal.comparable);QCOMPARE(result->terminal.complete,std::int64_t{5});
    QCOMPARE(server.requests,2);QCOMPARE(server.challenges,1);QCOMPARE(credentialReads,1);QVERIFY(approvals>=4);QCOMPARE(server.authorizations.size(),qsizetype{2});QVERIFY(server.authorizations[0].isEmpty());QCOMPARE(server.authorizations[1],server.requiredAuthorization);QVERIFY(server.failure.isEmpty());client.close();
  }
  void basicAuthorizationNeverCrossesRedirectOriginAndWrongPasswordStops(){
    HttpServer first,second;first.basicRealm="owned";first.requiredAuthorization="Basic "+QByteArray("owned-user:owned-password").toBase64();first.location="http://localhost:"+QByteArray::number(second.serverPort())+"/target";
    auto route=detail::libraryHttpRoute(policy(),first.url());QVERIFY(route);auto doc=core::Document::parse(savedAuthentication(route->scrape,first.basicRealm).toStdString());QVERIFY(doc);detail::LibraryHttpTracker client;std::optional<detail::LibraryHttpTracker::Result> result;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[](const auto&){return true;},[&](const auto&,const auto& actual,const auto& realm){return detail::libraryHttpSavedBasic(&*doc,actual,realm);});if(!value){return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY(result&&result->terminal.comparable);QCOMPARE(first.requests,2);QCOMPARE(second.requests,1);QVERIFY(second.authorizations[0].isEmpty());QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty());
    first.location.clear();doc=core::Document::parse(savedAuthentication(route->scrape,first.basicRealm,"owned-user","wrong").toStdString());QVERIFY(doc);result.reset();QVERIFY(client.start(request(2),*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY(result);QVERIFY(result->terminal.definitiveFailure&&!result->terminal.comparable);QCOMPARE(first.requests,4);QCOMPARE(first.challenges,3);
    doc=core::Document::parse(savedAuthentication(route->scrape,"different-realm").toStdString());QVERIFY(doc);result.reset();QVERIFY(client.start(request(3),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY(result&&result->terminal.definitiveFailure);QCOMPARE(first.requests,5);QCOMPARE(first.challenges,4);client.close();
  }
  void changedSavedCredentialsAndCancellationRevokeBeforeRetrySocket(){
    HttpServer server;server.basicRealm="owned";server.requiredAuthorization="Basic "+QByteArray("owned-user:owned-password").toBase64();auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);
    QTemporaryDir dir;QVERIFY(dir.isValid());auto lease=storage::ProfileLease::acquire(dir.path().toStdString());QVERIFY(lease);const auto path=dir.filePath("biglybt.config");test::write(path,savedAuthentication(route->scrape,server.basicRealm));
    auto config=detail::libraryHttpConfiguration(*lease);QVERIFY(config);detail::LibraryHttpTracker client;QVERIFY(client.start(request(),*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QVERIFY(client.poll(millis(),wallMillis(),[](const auto&){return true;}));
    QTRY_VERIFY_WITH_TIMEOUT(client.authenticationProposalPending(),10000);QCOMPARE(server.requests,1);int reads=0;
    test::write(path,savedAuthentication(route->scrape,server.basicRealm,"owned-user","changed"));
    auto withdrawn=client.poll(millis(),wallMillis(),[&](const auto&){auto current=detail::libraryHttpConfiguration(*lease);return current&&current->scope==config->scope;},[&](const auto&,const auto& actual,const auto& realm){++reads;auto doc=core::Document::parse(savedAuthentication(actual.scrape,realm).toStdString());return detail::libraryHttpSavedBasic(&*doc,actual,realm);});
    QVERIFY(withdrawn&&!*withdrawn);QVERIFY(!client.active());QCOMPARE(reads,0);QCOMPARE(server.requests,1);
    QVERIFY(client.start(request(2),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QVERIFY(client.poll(millis(),wallMillis(),[](const auto&){return true;}));QTRY_VERIFY_WITH_TIMEOUT(client.authenticationProposalPending(),10000);client.cancel();QVERIFY(!client.active());client.close();QCOMPARE(server.requests,2);QVERIFY(server.authorizations[0].isEmpty()&&server.authorizations[1].isEmpty());QVERIFY(server.failure.isEmpty());
  }
  void unchangedCacheCannotSurviveInternalTrackerPolicyChangeBeforeRetry(){
    HttpServer server;server.basicRealm="owned";server.requiredAuthorization="Basic "+QByteArray("owned-user:owned-password").toBase64();auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);
    QTemporaryDir dir;QVERIFY(dir.isValid());auto lease=storage::ProfileLease::acquire(dir.path().toStdString());QVERIFY(lease);const auto path=dir.filePath("biglybt.config");test::write(path,savedAuthentication(route->scrape,server.basicRealm));
    auto doc=core::Document::parse(test::read(path).toStdString());QVERIFY(doc);const auto cached=core::encodeValue(*doc->find("swt.auth.persistent.cache"),storage::compatibleConfigLimits());QVERIFY(cached);
    auto approved=detail::libraryHttpConfiguration(*lease);QVERIFY(approved);detail::LibraryHttpTracker client;QVERIFY(client.start(request(),*route,millis(),wallMillis()));
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QVERIFY(client.poll(millis(),wallMillis(),[](const auto&){return true;}));QTRY_VERIFY_WITH_TIMEOUT(client.authenticationProposalPending(),10000);int reads=0;
    auto selecting=client.poll(millis(),wallMillis(),[](const auto&){return true;},[&](const auto&,const auto& actual,const auto& realm){++reads;return detail::libraryHttpSavedBasic(&*doc,actual,realm);});QVERIFY(selecting);QCOMPARE(reads,1);
    // Credential is selected, but its second DNS result has no socket approval.
    QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QCOMPARE(server.requests,1);
    QVERIFY(doc->replaceValue("Tracker IP",krpc::string("localhost").toStdString()));test::write(path,QByteArray::fromStdString(doc->serialize()));
    const auto stillCached=core::encodeValue(*doc->find("swt.auth.persistent.cache"),storage::compatibleConfigLimits());QVERIFY(stillCached);QCOMPARE(*stillCached,*cached);
    auto changed=detail::libraryHttpConfiguration(*lease);QVERIFY(changed);QVERIFY(changed->scope!=approved->scope);QVERIFY(!detail::libraryHttpSavedBasic(&*doc,*route,server.basicRealm));
    auto withdrawn=client.poll(millis(),wallMillis(),[&](const auto&){auto fresh=detail::libraryHttpConfiguration(*lease);return fresh&&fresh->scope==approved->scope;});QVERIFY(withdrawn&&!*withdrawn);QVERIFY(!client.active());client.close();QCOMPARE(server.requests,1);QVERIFY(server.authorizations[0].isEmpty());QVERIFY(server.failure.isEmpty());
    QVERIFY(doc->replaceValue("Tracker IP","i1e"));test::write(path,QByteArray::fromStdString(doc->serialize()));auto typed=detail::libraryHttpConfiguration(*lease);QVERIFY(typed);QVERIFY(typed->scope!=changed->scope&&typed->scope!=approved->scope);
    QVERIFY(doc->replaceValue("Tracker IP",krpc::string("other.example").toStdString()));test::write(path,QByteArray::fromStdString(doc->serialize()));auto other=detail::libraryHttpConfiguration(*lease);QVERIFY(other);QVERIFY(other->scope!=typed->scope&&other->scope!=changed->scope);
  }
  void thirdAuthenticationOriginExhaustionIsUnknownWithoutAThirdCredential(){
    HttpServer first,second,third;const auto header="Basic "+QByteArray("owned-user:owned-password").toBase64();for(auto* server:{&first,&second,&third}){server->basicRealm="owned";server->requiredAuthorization=header;}
    first.location="http://localhost:"+QByteArray::number(second.serverPort())+"/second";second.location="http://localhost:"+QByteArray::number(third.serverPort())+"/third";
    auto route=detail::libraryHttpRoute(policy(),first.url());QVERIFY(route);detail::LibraryHttpTracker client;std::optional<detail::LibraryHttpTracker::Result> result;QString failure;int reads=0;
    const auto pump=[&]{auto value=client.poll(millis(),wallMillis(),[](const auto&){return true;},[&](const auto&,const auto& actual,const auto& realm){++reads;auto doc=core::Document::parse(savedAuthentication(actual.scrape,realm).toStdString());return detail::libraryHttpSavedBasic(&*doc,actual,realm);});if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY(client.start(request(),*route,millis(),wallMillis()));QTRY_VERIFY_WITH_TIMEOUT(!pump()||result.has_value(),15000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result);QVERIFY(!result->terminal.comparable&&!result->terminal.definitiveFailure);QCOMPARE(reads,2);QCOMPARE(first.requests,2);QCOMPARE(second.requests,2);QCOMPARE(third.requests,1);QCOMPARE(third.challenges,1);QVERIFY(third.authorizations[0].isEmpty());QVERIFY(first.failure.isEmpty()&&second.failure.isEmpty()&&third.failure.isEmpty());client.close();
  }
  void genuineNeverAdmittedOwnerUsesSavedBasicWithoutPayloadOrHiddenTorrent(){
    test::OwnedEngineFixture saved(false);HttpServer tracker;tracker.expectedHash=QByteArray(reinterpret_cast<const char*>(saved.hash->bytes().data()),20);tracker.basicRealm="owned";tracker.requiredAuthorization="Basic "+QByteArray("owned-user:owned-password").toBase64();tracker.body=counts(*saved.hash);
    auto metadata=core::Document::parse(test::read(saved.stateFile).toStdString());QVERIFY(metadata);QVERIFY(metadata->replaceValue("announce",test::encoded(libtorrent::entry(tracker.url()))));test::write(saved.stateFile,QByteArray::fromStdString(metadata->serialize()));
    auto route=detail::libraryHttpRoute(policy(),tracker.url());QVERIFY(route);test::write(saved.profile+"/biglybt.config",savedAuthentication(route->scrape,tracker.basicRealm));
    const SavedBytes inventory(saved.profile+"/downloads.config"),state(saved.stateFile),statistics(saved.profile+"/BiglyBT.statistics"),tables(saved.profile+"/tables.config"),configuration(saved.profile+"/biglybt.config");
    const auto bytes=test::read(saved.payload+"/sample.bin");auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);PayloadWatch watch(saved);
    auto p=policy();p.mainlineIpv4=true;p.mainlineIpv6=false;p.mainlinePeerBootstrapOnly=true;QTcpServer transfer,discovery;QVERIFY(transfer.listen(QHostAddress::LocalHost,0));QVERIFY(discovery.listen(QHostAddress::LocalHost,0));QUdpSocket reserve;QVERIFY(reserve.bind(QHostAddress::LocalHost,discovery.serverPort(),QUdpSocket::DontShareAddress));p.listenPort=transfer.serverPort();p.mainlinePort=discovery.serverPort();transfer.close();discovery.close();reserve.close();
    auto created=engine::PersistentSession::create({7981,7982,7983});QVERIFY(created);auto owner=std::move(*created);QVERIFY(owner->configureNetwork(p));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*lease));QString failure;
    const auto pump=[&]{if(!owner->poll()){failure="Saved Basic owner poll failed";return false;}const auto status=owner->mainlineDiscovery();if(status.failure||status.queuedScrapeFailure){failure=QString::fromStdString(status.failure?status.failure->message:status.queuedScrapeFailure->message);return false;}if(!owner->managedDownloads().empty()||!owner->pendingMagnets().empty()||!watch.untouched()){failure="Saved Basic acquired payload authority";return false;}return true;};
    QTRY_VERIFY_WITH_TIMEOUT(!pump()||tracker.requests==2,25000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QCOMPARE(tracker.requests,2);QCOMPARE(tracker.challenges,1);QCOMPARE(tracker.authorizations.size(),qsizetype{2});QVERIFY(tracker.authorizations[0].isEmpty());QCOMPARE(tracker.authorizations[1],tracker.requiredAuthorization);QVERIFY(tracker.failure.isEmpty());
    QCOMPARE(owner->mainlineDiscovery().lookups,std::uint64_t{0});QCOMPARE(owner->mainlineDiscovery().announces,std::uint64_t{0});QCOMPARE(owner->mainlineDiscovery().deliveredPeers,std::uint64_t{0});
    const auto closed=owner->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(watch.untouched());watch.stop();QCOMPARE(test::read(saved.payload+"/sample.bin"),bytes);QVERIFY(inventory.unchanged()&&state.unchanged()&&statistics.unchanged()&&tables.unchanged()&&configuration.unchanged());QVERIFY(saved.intact(false));
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
  void originalJksTrustUsesLastAliasAndFirstKeyCertificateAndChecksIntegrity(){
    Certificate first,second;const auto a=first.certificate.toDer(),b=second.certificate.toDer();
    for(const quint32 version:{1,2}){
      const auto encoded=jksStore({{"localhost:443",{a},false}},version);auto decoded=detail::http_trust::decode(encoded.toStdString());QVERIFY2(decoded,decoded?"":decoded.error().message.c_str());QCOMPARE(decoded->size(),1);QCOMPARE(decoded->front().toDer(),a);
      auto duplicate=detail::http_trust::decode(jksStore({{"same",{a},false},{"same",{b},false}},version).toStdString());QVERIFY(duplicate);QCOMPARE(duplicate->size(),1);QCOMPARE(duplicate->front().toDer(),b);
      auto unreachable=detail::http_trust::decode(jksStore({{"UPPER",{a},false}},version).toStdString());QVERIFY(unreachable&&unreachable->isEmpty());
      auto canonical=detail::http_trust::decode(jksStore({{"UPPER",{a},false},{"upper",{b},false}},version).toStdString());QVERIFY(canonical);QCOMPARE(canonical->size(),1);QCOMPARE(canonical->front().toDer(),b);
      auto chain=detail::http_trust::decode(jksStore({{"private",{a,b},true}},version).toStdString());QVERIFY(chain);QCOMPARE(chain->size(),1);QCOMPARE(chain->front().toDer(),a);
    }
    auto empty=detail::http_trust::decode(jksStore({}).toStdString());QVERIFY(empty&&empty->isEmpty());
    auto damaged=jksStore({{"one",{a},false}});damaged[damaged.size()-1]^=1;QVERIFY(!detail::http_trust::decode(damaged.toStdString()));
    const auto malformed=jksStore({{"one",{QByteArray("not DER")},false}});QVERIFY(!detail::http_trust::decode(malformed.toStdString()));
    auto trailing=jksStore({});trailing.insert(trailing.size()-20,'x');auto body=trailing.left(trailing.size()-20);trailing=body+QCryptographicHash::hash(QByteArray::fromHex("006300680061006e0067006500690074")+"Mighty Aphrodite"+body,QCryptographicHash::Sha1);QVERIFY(!detail::http_trust::decode(trailing.toStdString()));
    QByteArray excessive=QByteArray::fromHex("feedfeed0000000200001001");excessive+=QCryptographicHash::hash(QByteArray::fromHex("006300680061006e0067006500690074")+"Mighty Aphrodite"+excessive,QCryptographicHash::Sha1);QVERIFY(!detail::http_trust::decode(excessive.toStdString()));
    QVERIFY(!detail::http_trust::decode(jksStore({{"bad-utf-\xff",{a},false}}).toStdString()));
    const auto unicode=detail::http_trust::decode(jksStore({{QByteArray::fromHex("c3a9"),{a},false}}).toStdString());QVERIFY(!unicode);QCOMPARE(unicode.error().code,storage::ErrorCode::Unsupported);
  }
  void savedHttpsTrustIsProfileBoundReadOnlyAndCannotDisableOrdinaryHttp(){
    QTemporaryDir first,second;QVERIFY(first.isValid()&&second.isValid());auto a=storage::ProfileLease::acquire(first.path().toStdString()),b=storage::ProfileLease::acquire(second.path().toStdString());QVERIFY(a&&b);
    Certificate identity;const auto encoded=jksStore({{"localhost:443",{identity.certificate.toDer()},false}});const auto leaf=first.path()+"/.certs";
    auto http=detail::libraryHttpConfiguration(*a);QVERIFY(http);test::write(leaf,encoded);const SavedBytes saved(leaf);
    auto trusted=detail::libraryHttpConfiguration(*a,true),absent=detail::libraryHttpConfiguration(*b,true);QVERIFY2(trusted&&absent,!trusted?trusted.error().message.c_str():(!absent?absent.error().message.c_str():""));QVERIFY(trusted->trustAnchors);QCOMPARE(trusted->trustAnchors->size(),1);QVERIFY(!absent->trustAnchors);QVERIFY(trusted->scope!=absent->scope);QVERIFY(saved.unchanged());
    auto plain=detail::libraryHttpConfiguration(*a);QVERIFY(plain);QCOMPARE(plain->scope,http->scope);QVERIFY(!plain->trustAnchors);
    QVERIFY(QFile::rename(leaf,leaf+".old"));test::write(leaf,encoded);auto replaced=detail::libraryHttpConfiguration(*a,true);QVERIFY(replaced);QVERIFY(replaced->scope!=trusted->scope);QCOMPARE(test::read(leaf+".old"),encoded);
    test::write(leaf,"broken saved store");const SavedBytes bad(leaf);QVERIFY(!detail::libraryHttpConfiguration(*a,true));QVERIFY(detail::libraryHttpConfiguration(*a));QVERIFY(bad.unchanged());
    QVERIFY(QFile::remove(leaf));const auto external=second.path()+"/.certs";test::write(external,encoded);const SavedBytes foreign(external);
    QVERIFY(::symlink(external.toStdString().c_str(),leaf.toStdString().c_str())==0);auto link=detail::libraryHttpConfiguration(*a,true);QVERIFY(!link);QCOMPARE(link.error().code,storage::ErrorCode::UnsafePath);QVERIFY(foreign.unchanged());QVERIFY(QFile::remove(leaf));
    QVERIFY(::link(external.toStdString().c_str(),leaf.toStdString().c_str())==0);QVERIFY(!detail::libraryHttpConfiguration(*a,true));QVERIFY(QFile::remove(leaf));QVERIFY(QDir().mkdir(leaf));QVERIFY(!detail::libraryHttpConfiguration(*a,true));
    QVERIFY(QDir().rmdir(leaf));auto missing=detail::libraryHttpConfiguration(*a,true);QVERIFY(missing&&!missing->trustAnchors);
  }
  void savedJksAnchorsDriveActualTlsAndFreshGatesRevokeReplacedTrust(){
    QVERIFY(QSslSocket::supportsSsl());QTemporaryDir profile;QVERIFY(profile.isValid());auto lease=storage::ProfileLease::acquire(profile.path().toStdString());QVERIFY(lease);
    Certificate identity,other;HttpServer server(&identity);const auto leaf=profile.path()+"/.certs";const auto encoded=jksStore({{"localhost",{identity.certificate.toDer()},false}});
    test::write(leaf,encoded);const auto route=detail::libraryHttpRoute(policy(),server.url());QVERIFY(route);detail::LibraryHttpTracker client;
    const auto global=QSslConfiguration::defaultConfiguration().caCertificates();std::optional<detail::LibraryHttpTracker::Result> result;QString failure;
    const auto issue=[&](std::uint64_t nonce,const detail::LibraryHttpRoute& r){auto config=detail::libraryHttpConfiguration(*lease,true);if(!config){failure=QString::fromStdString(config.error().message);return false;}auto next=request(nonce);next.configuration=detail::libraryHttpRouteScope(r,config->scope);auto started=client.start(std::move(next),r,millis(),wallMillis(),config->singleOnly,config->trustAnchors);if(!started){failure=QString::fromStdString(started.error().message);return false;}return true;};
    const auto pump=[&](const detail::LibraryHttpRoute& r){auto value=client.poll(millis(),wallMillis(),[&](const auto& current){auto fresh=detail::libraryHttpConfiguration(*lease,true);return fresh&&current.configuration==detail::libraryHttpRouteScope(r,fresh->scope);});if(!value){failure=QString::fromStdString(value.error().message);return false;}if(*value){result=std::move(**value);}return true;};
    QVERIFY2(issue(1,*route),qPrintable(failure));QTRY_VERIFY_WITH_TIMEOUT(client.resolvedProposalPending(),10000);QCOMPARE(server.connections,0);
    QVERIFY(QFile::rename(leaf,leaf+".old"));test::write(leaf,encoded);QVERIFY(pump(*route));QVERIFY(!client.active()&&!result);QCOMPARE(server.connections,0);
    const SavedBytes saved(leaf);QVERIFY(issue(2,*route));QTRY_VERIFY_WITH_TIMEOUT(!pump(*route)||result.has_value(),10000);QVERIFY2(failure.isEmpty(),qPrintable(failure));QVERIFY(result&&result->terminal.comparable);QCOMPARE(server.requests,1);QVERIFY(saved.unchanged());result.reset();
    const auto wrong=detail::libraryHttpRoute(policy(),server.url(true));QVERIFY(wrong);QVERIFY(issue(3,*wrong));QTRY_VERIFY_WITH_TIMEOUT(!pump(*wrong)||result.has_value(),10000);QVERIFY(failure.isEmpty());QVERIFY(result&&result->terminal.definitiveFailure&&!result->terminal.comparable);QCOMPARE(server.requests,1);result.reset();
    test::write(leaf,jksStore({{"different-root",{other.certificate.toDer()},false}}));QVERIFY(issue(4,*route));QTRY_VERIFY_WITH_TIMEOUT(!pump(*route)||result.has_value(),10000);QVERIFY(failure.isEmpty());QVERIFY(result&&result->terminal.definitiveFailure&&!result->terminal.comparable);QCOMPARE(server.requests,1);result.reset();
    test::write(leaf,encoded);server.hold=true;QVERIFY(issue(5,*route));QTRY_VERIFY_WITH_TIMEOUT(!pump(*route)||server.requests==2,10000);QVERIFY(failure.isEmpty());QCOMPARE(server.requests,2);QVERIFY(!result);
    test::write(leaf,jksStore({}));server.release();QTRY_VERIFY_WITH_TIMEOUT(!pump(*route)||!client.active(),10000);QVERIFY(failure.isEmpty());QVERIFY(!result);QVERIFY(!client.active());QCOMPARE(server.requests,2);
    client.close();auto baseline=QSslConfiguration::defaultConfiguration();baseline.setCaCertificates({identity.certificate});QVERIFY(client.testTlsConfiguration(baseline));
    QVERIFY(issue(6,*route));QTRY_VERIFY_WITH_TIMEOUT(!pump(*route)||result.has_value(),10000);QVERIFY(failure.isEmpty());QVERIFY(result&&result->terminal.definitiveFailure&&!result->terminal.comparable);QCOMPARE(server.requests,2);
    client.close();QVERIFY(server.failure.isEmpty());QCOMPARE(QSslConfiguration::defaultConfiguration().caCertificates(),global);QCOMPARE(test::read(leaf+".old"),encoded);
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
  void actualJdk17KeytoolJksMatchesItsExportedTrustedCertificate() {
    const auto storePath=QFINDTESTDATA("fixtures/jks/jdk17-owned-truststore.jks");
    const auto certificatePath=QFINDTESTDATA("fixtures/jks/jdk17-owned-root.der");
    QVERIFY2(!storePath.isEmpty(),"Actual JDK17 public truststore fixture is missing");
    QVERIFY2(!certificatePath.isEmpty(),"Actual JDK17 public certificate fixture is missing");
    const auto store=test::read(storePath),certificate=test::read(certificatePath);
    auto decoded=detail::http_trust::decode(store.toStdString());
    QVERIFY2(decoded,decoded?"":decoded.error().message.c_str());
    QCOMPARE(decoded->size(),qsizetype{1});
    QCOMPARE(decoded->front().toDer(),certificate);
  }

};
}
int main(int argc,char** argv){
  QCoreApplication application(argc,argv);const auto privateRoot=qEnvironmentVariable("BIGLYBTC_HTTP_PRIVATE_RUNTIME");
  if(!privateRoot.isEmpty()){QCoreApplication::setLibraryPaths({privateRoot+"/qt/plugins"});}
  MainlineSingleHttpTests subject;return QTest::qExec(&subject,argc,argv);
}
#include "mainline_single_http_tests.moc"
