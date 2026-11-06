// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if !defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
#error Subscription endpoint fixtures belong only to the separate test image.
#endif
#include "rss_http_fetch_fixture.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <openssl/core_names.h>
#include <algorithm>
#include <map>

namespace subscription_http_fixture {
namespace s=biglybtc::search;
namespace wire=rss_fixture;
inline QByteArray dictionary(std::initializer_list<std::pair<QByteArray,QByteArray>> fields){
  std::vector<std::pair<QByteArray,QByteArray>> sorted(fields);
  std::ranges::sort(sorted,[](const auto& a,const auto& b){return a.first<b.first;});
  QByteArray result="d",previous;
  for(const auto& [key,value]:sorted){wire::require(previous.isEmpty()||previous!=key,"Duplicate fixture bencode key");result+=wire::b(key)+value;previous=key;}
  return result+'e';
}
inline QByteArray integer(qint64 value){return 'i'+QByteArray::number(value)+'e';}
inline void bigEndian32(QByteArray& bytes,quint32 value){for(int shift=24;shift>=0;shift-=8){bytes.append(static_cast<char>(value>>shift));}}

// A genuine local saved subscription fixture. No metasearch row is created by
// default; the embedded engine body is the only source of its literal GET.
struct Profile {
  QTemporaryDir root;QString path,bodyPath,resultsPath;
  std::optional<biglybtc::storage::ProfileLease> lease;
  std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> secret{nullptr,EVP_PKEY_free};
  QByteArray publicKey,shortId,metadataRow,configuration,body,settings="de";
  static constexpr std::int64_t engineId=-7;
  explicit Profile(const std::string& url,const QSslCertificate* trust=nullptr,bool autoDownload=false,const QJsonObject& filters={}){
    wire::require(root.isValid(),"Cannot create subscription transport fixture");path=root.filePath("profile");
    wire::require(QDir().mkpath(path+"/subs"),"Cannot create real subscription directory");
    secret.reset(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime192v2"));
    wire::require(bool(secret),"Cannot generate Original-compatible subscription key");
    wire::require(EVP_PKEY_set_utf8_string_param(secret.get(),OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,"uncompressed")==1,"Cannot choose uncompressed subscription public key");
    std::size_t length=0;wire::require(EVP_PKEY_get_octet_string_param(secret.get(),OSSL_PKEY_PARAM_PUB_KEY,nullptr,0,&length)==1&&length==49,"Unexpected prime192v2 public key size");
    publicKey.resize(static_cast<qsizetype>(length));
    wire::require(EVP_PKEY_get_octet_string_param(secret.get(),OSSL_PKEY_PARAM_PUB_KEY,reinterpret_cast<unsigned char*>(publicKey.data()),length,&length)==1&&publicKey.front()=='\x04',"Cannot encode subscription public key");
    shortId=QCryptographicHash::hash(publicKey,QCryptographicHash::Sha1).left(10);
    const auto name=QString::fromLatin1(shortId.toHex().toUpper());bodyPath=path+"/subs/"+name+".biglybt";resultsPath=path+"/subs/"+name+".results";
    const auto engine=engineRow(url,"embedded-rss");
    const QJsonObject definition{{"engine_id",qint64{engineId}},{"search_term",QString{}},{"filters",filters},{"options",QJsonObject{}},
      {"schedule",QJsonObject{{"interval",qint64{60}},{"days",QJsonArray{"1","2","3","4","5","6","7"}}}},
      {"engines",QJsonObject{{QString::number(static_cast<qlonglong>(engineId)),QJsonObject{{"content",QString::fromLatin1(engine.toBase64())}}}}}};
    const auto json=QJsonDocument(definition).toJson(QJsonDocument::Compact);
    const auto details=dictionary({{"az_version",integer(1)},{"is_anonymous",integer(0)},{"is_public",integer(0)},
      {"json",wire::b(json)},{"name",wire::b("Owned Subscription")},{"public_key",wire::b(publicKey)},{"version",integer(1)}});
    const auto hash=QCryptographicHash::hash(details,QCryptographicHash::Sha1);QByteArray encoded=hash;
    bigEndian32(encoded,1);bigEndian32(encoded,static_cast<quint32>(details.size()));
    const auto signature=sign(encoded);
    const auto content=dictionary({{"details",details},{"hash",wire::b(hash)},{"sig",wire::b(signature)},{"size",integer(details.size())}});
    body=dictionary({{"vuze",dictionary({{"components","l"+dictionary({{"content",content},{"type",integer(16)}})+"e"}})}});
    const auto history=dictionary({{"auto_dl",integer(autoDownload?1:0)},{"auto_dl_supported",integer(0)},{"dl_with_ref",integer(1)},
      {"enabled",integer(1)},{"last_new",integer(0)},{"last_scan",integer(0)},{"max_results",integer(-1)},{"num_read",integer(0)},{"num_unread",integer(0)}});
    metadataRow=dictionary({{"az_version",integer(1)},{"fixture_future",wire::b("preserve")},{"hash",wire::b(hash)},{"history",history},
      {"is_anonymous",integer(0)},{"is_public",integer(0)},{"name",wire::b("Owned Subscription")},{"public_key",wire::b(publicKey)},
      {"sig",wire::b(signature)},{"sig_data_size",integer(details.size())},{"subscribed",integer(1)},{"version",integer(1)}});
    configuration=dictionary({{"subs","l"+metadataRow+"e"}});restore();wire::write(resultsPath,dictionary({{"results","le"}}));
    if(trust){wire::write(path+"/.certs",wire::jks(*trust));}
    auto acquired=biglybtc::storage::ProfileLease::acquire(path.toStdString());wire::require(bool(acquired),"Cannot acquire real subscription fixture lease");lease.emplace(std::move(*acquired));
  }
  static QByteArray engineRow(const std::string& url,const QByteArray& uid){
    return dictionary({{"id",integer(engineId)},{"name",wire::b("Owned RSS Engine")},{"selected",integer(0)},{"type",integer(4)},
      {"uid",wire::b(uid)},{"web.maps","le"},{"web.search_url_format",wire::b(QByteArray::fromStdString(url))}});
  }
  void catalog(const std::string& url){wire::write(path+"/metasearch.config",dictionary({{"engines","l"+engineRow(url,"catalog-rss")+"e"}}));}
  void restore(){wire::write(path+"/subscriptions.config",configuration);wire::write(bodyPath,body);wire::write(path+"/biglybt.config",settings);}
  s::SavedSubscriptionSelector selector(std::size_t sourceIndex=0)const{return {sourceIndex,shortId.toStdString(),publicKey.toStdString(),std::nullopt,1};}
  std::map<QString,std::optional<QByteArray>> sourceBytes()const{
    std::map<QString,std::optional<QByteArray>> result;
    for(const auto& stem:QStringList{path+"/subscriptions.config",bodyPath,resultsPath,path+"/biglybt.config",path+"/metasearch.config",path+"/.certs",path+"/.subscriptions-native-write"}){
      for(const auto& suffix:QStringList{QString{},".bak",".saving"}){
        const auto name=stem+suffix;QFile file(name);if(file.exists()){wire::require(file.open(QIODevice::ReadOnly),"Cannot inspect private source bytes");result[name]=file.readAll();}else{result[name]=std::nullopt;}
      }
    }return result;
  }
  s::RssHttpFetch::PrivateEndpointFixture endpoint()const{
    s::RssHttpFetch::PrivateEndpointFixture fixture;fixture.endpoint=QHostAddress::LocalHost;
    fixture.resolve=[](const QString& host,QObject* context,s::RssHttpFetch::PrivateEndpointFixture::Completion completion){
      static std::atomic<int> next{1};const int id=next.fetch_add(1);QHostInfo answer;answer.setHostName(host);answer.setAddresses({QHostAddress::LocalHost});const QPointer<QObject> guarded(context);
      if(!QMetaObject::invokeMethod(context,[guarded,completion=std::move(completion),answer]{if(guarded){completion(answer);}},Qt::QueuedConnection)){return -1;}return id;
    };
    fixture.cancelResolve=[](int){};return fixture;
  }
private:
  QByteArray sign(const QByteArray& bytes)const{
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    wire::require(context&&EVP_DigestSignInit(context.get(),nullptr,EVP_sha1(),nullptr,secret.get())==1&&EVP_DigestSignUpdate(context.get(),bytes.constData(),static_cast<std::size_t>(bytes.size()))==1,"Cannot initialise subscription signature");
    std::size_t size=0;wire::require(EVP_DigestSignFinal(context.get(),nullptr,&size)==1&&size>0&&size<4096,"Cannot size subscription signature");
    QByteArray result(static_cast<qsizetype>(size),'\0');wire::require(EVP_DigestSignFinal(context.get(),reinterpret_cast<unsigned char*>(result.data()),&size)==1,"Cannot sign subscription fixture");result.resize(static_cast<qsizetype>(size));return result;
  }
};
} // namespace subscription_http_fixture
