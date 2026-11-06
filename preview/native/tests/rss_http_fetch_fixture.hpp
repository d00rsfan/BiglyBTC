// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if !defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
#error This owned endpoint fixture belongs only to the separate test image.
#endif
#include "rss_http_fetch.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QVariant>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <zlib.h>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <mutex>

namespace rss_fixture {
namespace s=biglybtc::search;
inline void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
inline QByteArray b(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
inline QByteArray dict(std::initializer_list<std::pair<QByteArray,QByteArray>> fields){QByteArray out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
inline void write(const QString& path,const QByteArray& bytes){QFile file(path);require(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size()&&file.flush(),"Cannot write private RSS fixture");}
inline QByteArray gzip(const QByteArray& raw){
  z_stream stream{};require(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"Cannot initialise fixture gzip");
  struct End{z_stream* value;~End(){deflateEnd(value);}} end{&stream};
  QByteArray encoded(static_cast<qsizetype>(compressBound(static_cast<uLong>(raw.size()))+64),'\0');
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));stream.avail_in=static_cast<uInt>(raw.size());stream.next_out=reinterpret_cast<Bytef*>(encoded.data());stream.avail_out=static_cast<uInt>(encoded.size());
  require(deflate(&stream,Z_FINISH)==Z_STREAM_END,"Fixture gzip exceeded its output");encoded.resize(static_cast<qsizetype>(stream.total_out));return encoded;
}
struct Certificate {
  QSslCertificate certificate;QSslKey key;
  explicit Certificate(const char* host="feeds.example.org"){
    const auto freeKey=[](EVP_PKEY* p){EVP_PKEY_free(p);};const auto freeCert=[](X509* p){X509_free(p);};const auto freeBio=[](BIO* p){BIO_free(p);};
    std::unique_ptr<EVP_PKEY,decltype(freeKey)> secret(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime256v1"),freeKey);
    std::unique_ptr<X509,decltype(freeCert)> cert(X509_new(),freeCert);require(secret&&cert,"Cannot generate fixture TLS identity");
    require(X509_set_version(cert.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1)==1&&X509_gmtime_adj(X509_getm_notBefore(cert.get()),-60)&&X509_gmtime_adj(X509_getm_notAfter(cert.get()),86400)&&X509_set_pubkey(cert.get(),secret.get())==1,"Cannot prepare fixture certificate");
    auto* subject=X509_get_subject_name(cert.get());require(X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>(host),-1,-1,0)==1&&X509_set_issuer_name(cert.get(),subject)==1,"Cannot set fixture TLS subject");
    const auto san=QByteArray("DNS:")+host;
    for(const auto& [type,text]:std::vector<std::pair<int,QByteArray>>{{NID_subject_alt_name,san},{NID_basic_constraints,"critical,CA:TRUE"},{NID_key_usage,"critical,digitalSignature,keyCertSign"},{NID_ext_key_usage,"serverAuth"}}){
      auto* extension=X509V3_EXT_conf_nid(nullptr,nullptr,type,const_cast<char*>(text.constData()));require(extension&&X509_add_ext(cert.get(),extension,-1)==1,"Cannot set fixture TLS extension");X509_EXTENSION_free(extension);
    }
    require(X509_sign(cert.get(),secret.get(),EVP_sha256())>0,"Cannot sign fixture certificate");
    std::unique_ptr<BIO,decltype(freeBio)> certBio(BIO_new(BIO_s_mem()),freeBio),keyBio(BIO_new(BIO_s_mem()),freeBio);
    require(certBio&&keyBio&&PEM_write_bio_X509(certBio.get(),cert.get())==1&&PEM_write_bio_PrivateKey(keyBio.get(),secret.get(),nullptr,nullptr,0,nullptr,nullptr)==1,"Cannot encode fixture certificate");
    char* certBytes=nullptr;char* keyBytes=nullptr;const auto certSize=BIO_get_mem_data(certBio.get(),&certBytes),keySize=BIO_get_mem_data(keyBio.get(),&keyBytes);
    certificate=QSslCertificate(QByteArray(certBytes,static_cast<qsizetype>(certSize)));key=QSslKey(QByteArray(keyBytes,static_cast<qsizetype>(keySize)),QSsl::Ec);
    require(!certificate.isNull()&&!key.isNull(),"Qt refused fixture TLS identity");
  }
};
inline QByteArray jks(const QSslCertificate& certificate){
  QByteArray body;QDataStream stream(&body,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::BigEndian);
  const auto utf=[&](const QByteArray& text){stream<<static_cast<quint16>(text.size());require(stream.writeRawData(text.constData(),static_cast<int>(text.size()))==text.size(),"Cannot encode fixture JKS text");};
  stream<<quint32{0xfeedfeed}<<quint32{2}<<quint32{1}<<quint32{2};utf("owned");stream<<quint64{1700000000000};utf("X.509");const auto der=certificate.toDer();stream<<static_cast<quint32>(der.size());require(stream.writeRawData(der.constData(),static_cast<int>(der.size()))==der.size(),"Cannot encode fixture JKS certificate");
  return body+QCryptographicHash::hash(QByteArray::fromHex("006300680061006e0067006500690074")+"Mighty Aphrodite"+body,QCryptographicHash::Sha1);
}
inline QByteArray rss(){return "<rss><channel><item><title>Owned</title><enclosure type=\"application/x-bittorrent\" url=\"relative.torrent\"/></item></channel></rss>";}
class Server final:public QTcpServer {
public:
  QByteArray body=rss(),location,encoding,rawReply;QList<QByteArray> requests;QString failure;
  bool chunked=false,holdReply=false,holdBody=false,redirectEachRequest=false;int connections=0,encrypted=0,bodyPrefixBytes=0;
  const Certificate* tls=nullptr;
  QList<QPointer<QTcpSocket>> held;
  explicit Server(const Certificate* certificate=nullptr):tls(certificate){require(listen(QHostAddress::LocalHost,0),"Cannot bind private RSS endpoint");}
  std::string url(const char* path="/configured/feed.xml?literal=1")const{return std::string(tls?"https://":"http://")+"feeds.example.org:"+std::to_string(serverPort())+path;}
  void release(){holdReply=false;holdBody=false;const auto sockets=held;held.clear();for(const auto& socket:sockets){if(socket&&socket->state()==QAbstractSocket::ConnectedState){reply(socket,false);}}}
protected:
  void incomingConnection(qintptr descriptor)override{
    ++connections;QTcpSocket* socket=nullptr;
    if(tls){auto* ssl=new QSslSocket(this);ssl->setLocalCertificate(tls->certificate);ssl->setPrivateKey(tls->key);ssl->setPeerVerifyMode(QSslSocket::VerifyNone);socket=ssl;QObject::connect(ssl,&QSslSocket::encrypted,this,[this]{++encrypted;});}
    else{socket=new QTcpSocket(this);}
    if(!socket->setSocketDescriptor(descriptor)){failure="Private socket descriptor failed";socket->deleteLater();return;}
    auto bytes=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this,socket,bytes,done]{
      *bytes+=socket->readAll();if(bytes->size()>8192){failure="Unbounded RSS request";socket->abort();return;}
      if(*done||!bytes->contains("\r\n\r\n")){return;}*done=true;requests.push_back(*bytes);
      if(!bytes->startsWith("GET ")||bytes->contains("info_hash=")||bytes->startsWith("HEAD ")){failure="RSS emitted a tracker or non-GET request";socket->abort();return;}
      if(holdReply){held.push_back(socket);}else{reply(socket,true);}
    });
    QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    if(tls){static_cast<QSslSocket*>(socket)->startServerEncryption();}
  }
private:
  void reply(QTcpSocket* socket,bool initial){
    if(holdBody&&!initial){return;}
    const auto content=encoding.isEmpty()?QByteArray{}:"Content-Encoding: "+encoding+"\r\n";
    QByteArray output;
    if(!initial&&socket->property("partial").toBool()){output=body.mid(1);}
    else if(!rawReply.isEmpty()){output=rawReply;}
    else if(redirectEachRequest||!location.isEmpty()){const auto target=redirectEachRequest?"/hop/"+QByteArray::number(requests.size()):location;output="HTTP/1.1 302 Found\r\nLocation: "+target+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";}
    else if(chunked){output="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"+content+"Connection: close\r\n\r\n"+QByteArray::number(body.size(),16)+"\r\n"+body+"\r\n0\r\n\r\n";}
    else if(holdBody){output="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body.left(1);socket->setProperty("partial",true);bodyPrefixBytes=1;}
    else{output="HTTP/1.1 200 OK\r\n"+content+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;}
    if(socket->write(output)!=output.size()){failure="Private RSS response write failed";}
    if(holdBody){held.push_back(socket);}else{socket->disconnectFromHost();}
  }
};
struct Profile {
  QTemporaryDir root;QString path;std::optional<biglybtc::storage::ProfileLease> lease;
  explicit Profile(const std::string& url,const QSslCertificate* trust=nullptr){
    require(root.isValid(),"Cannot create private RSS profile");path=root.filePath("profile");require(QDir().mkpath(path),"Cannot create private RSS directory");
    const auto row=dict({{"type","i4e"},{"id","i7e"},{"uid",b("owned-rss")},{"name",b("Owned RSS")},{"selected","i0e"},{"web.maps","le"},{"web.search_url_format",b(QByteArray::fromStdString(url))}});
    write(path+"/metasearch.config",dict({{"engines","l"+row+"e"}}));write(path+"/biglybt.config","de");if(trust){write(path+"/.certs",jks(*trust));}
    auto held=biglybtc::storage::ProfileLease::acquire(path.toStdString());require(bool(held),"Cannot acquire real private RSS lease");lease.emplace(std::move(*held));
  }
  static s::SavedRssSelector selector(){return {0,7,"owned-rss"};}
  s::RssHttpFetch::PrivateEndpointFixture endpoint()const{
    s::RssHttpFetch::PrivateEndpointFixture out;out.endpoint=QHostAddress::LocalHost;
    out.resolve=[](const QString& host,QObject* context,s::RssHttpFetch::PrivateEndpointFixture::Completion completion){
      static std::atomic<int> next{1};const int id=next.fetch_add(1);QHostInfo answer;answer.setHostName(host);answer.setAddresses({QHostAddress::LocalHost});
      const QPointer<QObject> guard(context);
      if(!QMetaObject::invokeMethod(context,[guard,completion=std::move(completion),answer]{if(guard){completion(answer);}},Qt::QueuedConnection)){return -1;}return id;
    };
    out.cancelResolve=[](int){};return out;
  }
};
struct HeldResolver {
  struct Pending {int id;QPointer<QObject> context;s::RssHttpFetch::PrivateEndpointFixture::Completion completion;QHostInfo answer;};
  mutable std::mutex mutex;std::vector<Pending> pending;std::vector<int> cancellations;std::atomic<int> callbacksForwarded{0};int next=1;
  std::size_t count()const{std::lock_guard lock(mutex);return pending.size();}
  int release(){
    std::vector<Pending> ready;{std::lock_guard lock(mutex);ready.swap(pending);}int queued=0;
    for(auto& item:ready){const QPointer<QObject> context=item.context;if(context&&QMetaObject::invokeMethod(context,[this,item=std::move(item)]{if(item.context){++callbacksForwarded;item.completion(item.answer);}},Qt::QueuedConnection)){++queued;}}
    return queued;
  }
  s::RssHttpFetch::PrivateEndpointFixture endpoint(){
    s::RssHttpFetch::PrivateEndpointFixture out;out.endpoint=QHostAddress::LocalHost;
    out.resolve=[this](const QString& host,QObject* context,s::RssHttpFetch::PrivateEndpointFixture::Completion completion){
      QHostInfo answer;answer.setHostName(host);answer.setAddresses({QHostAddress::LocalHost});std::lock_guard lock(mutex);const int id=next++;pending.push_back({id,context,std::move(completion),answer});return id;
    };
    out.cancelResolve=[this](int id){std::lock_guard lock(mutex);cancellations.push_back(id);};return out;
  }
};
} // namespace rss_fixture
