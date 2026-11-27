// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracker_tls.hpp"

#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <utility>

namespace biglybtc::tracker::detail {
namespace {
using Key = std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
using Certificate = std::unique_ptr<X509,decltype(&X509_free)>;
using Code=storage::ErrorCode;
constexpr char Password[]="changeit"; // BiglyBT SESecurityManager.SSL_PASSWORD.
constexpr std::string_view JavaPassword("\0c\0h\0a\0n\0g\0e\0i\0t",16);
constexpr std::uint32_t JksMagic=0xfeedfeed;
constexpr std::size_t DigestSize=20;
constexpr std::size_t StoreLimit=2*1024*1024;

void require(bool condition,Code code,const char* reason) {
  if(!condition){throw storage::Error{code,reason};}
}

QByteArrayView view(std::string_view bytes) {
  return {bytes.data(),static_cast<qsizetype>(bytes.size())};
}

class KeyBytes {
public:
  QByteArray value;
  explicit KeyBytes(qsizetype length):value(length,Qt::Uninitialized) {}
  ~KeyBytes(){OPENSSL_cleanse(value.data(),static_cast<std::size_t>(value.size()));}
};

class Reader {
  std::string_view bytes_;
public:
  explicit Reader(std::string_view bytes):bytes_(bytes) {}
  std::string_view read(std::size_t count) {
    require(count<=bytes_.size(),Code::InvalidFormat,"Truncated Java tracker key store");
    const auto part=bytes_.substr(0,count);
    bytes_.remove_prefix(count);
    return part;
  }
  std::uint32_t number(std::size_t width=4) {
    std::uint32_t result=0;
    for(unsigned char byte:read(width)){result=(result<<8)|byte;}
    return result;
  }
  std::string_view text(){return read(number(2));}
  bool empty() const noexcept {return bytes_.empty();}
};

QByteArray sha1(std::initializer_list<QByteArrayView> parts) {
  QCryptographicHash hash(QCryptographicHash::Sha1);
  for(const auto part:parts){hash.addData(part);}
  return hash.result();
}

QSslCertificate parseCertificate(std::string_view der) {
  const auto chain=QSslCertificate::fromData(view(der).toByteArray(),QSsl::Der);
  require(chain.size()==1&&!chain.front().isNull(),Code::InvalidFormat,"Invalid tracker certificate");
  return chain.front();
}

QSslCertificate exportCertificate(X509* certificate) {
  require(certificate,Code::InvalidFormat,"Tracker key store contains no certificate");
  const auto length=i2d_X509(certificate,nullptr);
  require(length>0,Code::InvalidFormat,"Invalid tracker certificate");
  QByteArray encoded(length,Qt::Uninitialized);
  auto* cursor=reinterpret_cast<unsigned char*>(encoded.data());
  require(i2d_X509(certificate,&cursor)==length,Code::InvalidFormat,"Cannot export tracker certificate");
  return parseCertificate({encoded.constData(),static_cast<std::size_t>(encoded.size())});
}

QSslKey exportKey(EVP_PKEY* key) {
  require(key,Code::InvalidFormat,"Tracker key store contains no private key");
  QSsl::KeyAlgorithm algorithm;
  switch(EVP_PKEY_get_base_id(key)){
    case EVP_PKEY_RSA: algorithm=QSsl::Rsa;break;
    case EVP_PKEY_EC: algorithm=QSsl::Ec;break;
    case EVP_PKEY_DSA: algorithm=QSsl::Dsa;break;
    default: throw storage::Error{Code::Unsupported,"Tracker certificate key algorithm is unavailable"};
  }
  const auto length=i2d_PrivateKey(key,nullptr);
  require(length>0,Code::InvalidFormat,"Cannot export tracker private key");
  KeyBytes encoded(length);
  auto* cursor=reinterpret_cast<unsigned char*>(encoded.value.data());
  require(i2d_PrivateKey(key,&cursor)==length,Code::InvalidFormat,"Cannot export tracker private key");
  auto result=QSslKey(encoded.value,algorithm,QSsl::Der,QSsl::PrivateKey);
  require(!result.isNull(),Code::Unsupported,"Qt TLS cannot import the tracker private key");
  return result;
}

Key decodeJavaKey(std::string_view input) {
  const auto* begin=reinterpret_cast<const unsigned char*>(input.data());
  auto* cursor=begin;
  const std::unique_ptr<X509_SIG,decltype(&X509_SIG_free)> protectedKey(
      d2i_X509_SIG(nullptr,&cursor,static_cast<long>(input.size())),&X509_SIG_free);
  require(protectedKey&&cursor==begin+input.size(),Code::InvalidFormat,"Invalid protected Java tracker key");
  const X509_ALGOR* algorithm=nullptr;
  const ASN1_OCTET_STRING* cipher=nullptr;
  X509_SIG_get0(protectedKey.get(),&algorithm,&cipher);
  require(algorithm&&cipher,Code::InvalidFormat,"Invalid Java private-key envelope");
  const ASN1_OBJECT* identifier=nullptr;
  X509_ALGOR_get0(&identifier,nullptr,nullptr,algorithm);
  const std::unique_ptr<ASN1_OBJECT,decltype(&ASN1_OBJECT_free)> javaKeyProtector(
      OBJ_txt2obj("1.3.6.1.4.1.42.2.17.1.1",1),&ASN1_OBJECT_free);
  require(javaKeyProtector&&identifier&&OBJ_cmp(javaKeyProtector.get(),identifier)==0,Code::Unsupported,
      "Unsupported Java tracker private-key protection");
  const auto length=ASN1_STRING_length(cipher);
  require(length>static_cast<int>(2*DigestSize),Code::InvalidFormat,"Invalid protected Java tracker key length");
  const auto* raw=ASN1_STRING_get0_data(cipher);
  std::string_view bytes(reinterpret_cast<const char*>(raw),static_cast<std::size_t>(length));
  const auto encrypted=bytes.substr(DigestSize,bytes.size()-2*DigestSize);
  KeyBytes decoded(static_cast<qsizetype>(encrypted.size()));
  auto block=view(bytes.substr(0,DigestSize)).toByteArray();
  for(std::size_t offset=0;offset<encrypted.size();offset+=DigestSize){
    block=sha1({view(JavaPassword),block});
    const auto count=std::min(DigestSize,encrypted.size()-offset);
    for(std::size_t index=0;index<count;++index){
      decoded.value[static_cast<qsizetype>(offset+index)]=encrypted[offset+index]^block[static_cast<qsizetype>(index)];
    }
  }
  const auto expected=sha1({view(JavaPassword),decoded.value});
  require(CRYPTO_memcmp(expected.constData(),bytes.data()+bytes.size()-DigestSize,DigestSize)==0,Code::InvalidFormat,
      "Java tracker private-key integrity check failed");
  begin=reinterpret_cast<const unsigned char*>(decoded.value.constData());cursor=begin;
  Key result(d2i_AutoPrivateKey(nullptr,&cursor,decoded.value.size()),&EVP_PKEY_free);
  require(result&&cursor==begin+decoded.value.size(),Code::InvalidFormat,"Invalid Java tracker private key");
  return result;
}

std::pair<Key,QList<QSslCertificate>> javaIdentity(std::string_view input) {
  require(input.size()>DigestSize,Code::InvalidFormat,"Truncated Java tracker key store");
  const auto payload=input.substr(0,input.size()-DigestSize);
  const auto digest=sha1({view(JavaPassword),QByteArrayView("Mighty Aphrodite"),view(payload)});
  require(CRYPTO_memcmp(digest.constData(),input.data()+payload.size(),DigestSize)==0,Code::InvalidFormat,
      "Java tracker key store integrity check failed");
  Reader reader(payload);
  require(reader.number()==JksMagic,Code::InvalidFormat,"Invalid Java tracker key store");
  const auto version=reader.number();
  require(version==1||version==2,Code::Unsupported,"Unsupported Java tracker key store version");
  const auto count=reader.number();
  require(count<=64,Code::Unsupported,"Too many entries in the tracker key store");
  Key result(nullptr,&EVP_PKEY_free);
  QList<QSslCertificate> chain;
  const auto certificate=[&] {
    if(version==2){require(reader.text()=="X.509",Code::Unsupported,"Unsupported Java certificate type");}
    return parseCertificate(reader.read(reader.number()));
  };
  for(std::uint32_t index=0;index<count;++index){
    const auto type=reader.number();
    static_cast<void>(reader.text()); // Alias. The currently hosted tracker accepts one identity.
    static_cast<void>(reader.read(8)); // Timestamp.
    if(type==2){static_cast<void>(certificate());continue;}
    require(type==1&&!result,Code::Unsupported,"The saved tracker requires one private-key identity");
    result=decodeJavaKey(reader.read(reader.number()));
    const auto certificates=reader.number();
    require(certificates>0&&certificates<=32,Code::InvalidFormat,"Invalid Java tracker certificate chain");
    for(std::uint32_t depth=0;depth<certificates;++depth){chain.push_back(certificate());}
  }
  require(reader.empty()&&result,Code::InvalidFormat,"No complete Java tracker identity");
  return {std::move(result),std::move(chain)};
}

std::pair<Key,QList<QSslCertificate>> pkcs12Identity(std::string_view input) {
  const auto* begin=reinterpret_cast<const unsigned char*>(input.data());
  auto* cursor=begin;
  std::unique_ptr<PKCS12,decltype(&PKCS12_free)> store(d2i_PKCS12(nullptr,&cursor,static_cast<long>(input.size())),&PKCS12_free);
  require(store&&cursor==begin+input.size(),Code::InvalidFormat,"Invalid PKCS#12 tracker key store");
  EVP_PKEY* rawKey=nullptr;X509* rawLeaf=nullptr;STACK_OF(X509)* rawChain=nullptr;
  const auto parsed=PKCS12_parse(store.get(),Password,&rawKey,&rawLeaf,&rawChain);
  Key key(rawKey,&EVP_PKEY_free);
  Certificate leaf(rawLeaf,&X509_free);
  const auto freeChain=[](STACK_OF(X509)* values){sk_X509_pop_free(values,X509_free);};
  std::unique_ptr<STACK_OF(X509),decltype(freeChain)> chain(rawChain,freeChain);
  require(parsed==1&&key&&leaf,Code::InvalidFormat,"Cannot open the saved PKCS#12 tracker identity");
  QList<QSslCertificate> result{exportCertificate(leaf.get())};
  const auto count=chain?sk_X509_num(chain.get()):0;
  require(count>=0&&count<32,Code::Unsupported,"Too many tracker chain certificates");
  for(int i=0;i<count;++i){result.push_back(exportCertificate(sk_X509_value(chain.get(),i)));}
  return {std::move(key),std::move(result)};
}
}

std::expected<QSslConfiguration,storage::Error> loadTrackerTlsIdentity(std::string_view keyStore) try {
  require(keyStore.size()>=4&&keyStore.size()<=StoreLimit,Code::InvalidFormat,"Tracker key store exceeds supported bounds");
  require(QSslSocket::supportsSsl(),Code::Unsupported,"Qt TLS is unavailable for the owned tracker");
  Reader header(keyStore);
  auto [key,chain]=header.number()==JksMagic?javaIdentity(keyStore):pkcs12Identity(keyStore);
  require(!chain.empty(),Code::InvalidFormat,"Tracker key store contains no leaf certificate");
  const auto encoded=chain.front().toDer();
  const auto* certificateBytes=reinterpret_cast<const unsigned char*>(encoded.constData());
  const Certificate leaf(d2i_X509(nullptr,&certificateBytes,encoded.size()),&X509_free);
  require(leaf&&X509_check_private_key(leaf.get(),key.get())==1,Code::InvalidFormat,"Tracker certificate and key do not match");
  auto result=QSslConfiguration::defaultConfiguration();
  result.setProtocol(QSsl::TlsV1_2OrLater);
  result.setPeerVerifyMode(QSslSocket::VerifyNone);
  result.setLocalCertificateChain(chain);
  result.setPrivateKey(exportKey(key.get()));
  return result;
} catch(const storage::Error& error) {
  return std::unexpected(error);
} catch(...) {
  return std::unexpected(storage::Error{Code::Io,"Cannot prepare the saved tracker TLS identity"});
}
}
