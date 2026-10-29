#pragma once
#include "file_io.hpp"
#include <QCryptographicHash>
#include <QSslCertificate>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::engine::detail {
namespace http_trust {
inline constexpr std::size_t MaximumBytes=4*1024*1024,MaximumEntries=4096,MaximumCertificateBytes=256*1024;
inline std::unexpected<storage::Error> invalid(const char* message){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,message});}
// JavaKeyStore.engineLoad/readUTF: alias strings are entry keys, not hostnames.
// Decode modified UTF-8 to Java UTF-16 code units (including surrogate units).
struct Input {
  std::string_view bytes;std::size_t position=0;bool valid=true;
  std::string_view take(std::size_t count){if(count>bytes.size()-position){valid=false;return {};}const auto out=bytes.substr(position,count);position+=count;return out;}
  std::uint32_t number(){const auto raw=take(4);if(raw.size()!=4){return 0;}std::uint32_t value=0;for(const unsigned char c:raw){value=(value<<8)|c;}return value;}
  std::u16string utf(){
    const auto length=take(2);if(length.size()!=2){return {};}
    const auto count=(static_cast<unsigned char>(length[0])<<8)|static_cast<unsigned char>(length[1]);const auto raw=take(count);std::u16string out;
    for(std::size_t i=0;i<raw.size();){const auto a=static_cast<unsigned char>(raw[i++]);
      if(a<128){out+=static_cast<char16_t>(a);continue;}
      if((a&0xe0)==0xc0){if(i>=raw.size()||(static_cast<unsigned char>(raw[i])&0xc0)!=0x80){valid=false;return {};}
        out+=static_cast<char16_t>(((a&31)<<6)|(static_cast<unsigned char>(raw[i++])&63));continue;}
      if((a&0xf0)==0xe0){if(raw.size()-i<2||(static_cast<unsigned char>(raw[i])&0xc0)!=0x80||(static_cast<unsigned char>(raw[i+1])&0xc0)!=0x80){valid=false;return {};}
        out+=static_cast<char16_t>(((a&15)<<12)|((static_cast<unsigned char>(raw[i])&63)<<6)|(static_cast<unsigned char>(raw[i+1])&63));i+=2;continue;}
      valid=false;return {};
    }return out;
  }
};
inline QByteArray integrity(std::string_view body){
  QCryptographicHash digest(QCryptographicHash::Sha1);QByteArray password;
  for(const char c:std::string_view("changeit")){password+='\0';password+=c;}
  digest.addData(password);digest.addData(QByteArrayView("Mighty Aphrodite",15));
  digest.addData(QByteArrayView(body.data(),static_cast<qsizetype>(body.size())));return digest.result();
}
inline std::expected<QList<QSslCertificate>,storage::Error> decode(std::string_view bytes){
  if(bytes.size()<4||bytes.size()>MaximumBytes){return invalid("Saved HTTPS trust store exceeds supported JKS bounds");}
  if(bytes.substr(0,4)!=std::string_view("\xfe\xed\xfe\xed",4)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Saved HTTPS trust store must be original JKS (GKR/BKS/PKCS12 are unsupported)"});}
  if(bytes.size()<32){return invalid("Saved HTTPS JKS is truncated");}
  const auto body=bytes.substr(0,bytes.size()-20);const auto expected=integrity(body);if(expected.size()!=20){return invalid("Saved HTTPS JKS integrity algorithm is unavailable");}unsigned difference=0;
  for(std::size_t i=0;i<20;++i){difference|=static_cast<unsigned char>(expected[static_cast<qsizetype>(i)])^static_cast<unsigned char>(bytes[body.size()+i]);}
  if(difference){return invalid("Saved HTTPS JKS integrity/password check failed");}
  Input in{body};(void)in.number(); // Magic was checked before the keyed digest.
  const auto version=in.number();if(version!=1&&version!=2){return invalid("Saved HTTPS JKS version is unsupported");}
  const auto count=in.number();if(count>MaximumEntries){return invalid("Saved HTTPS JKS has too many entries");}
  std::map<std::u16string,QSslCertificate> entries;
  for(std::uint32_t index=0;index<count;++index){
    const auto tag=in.number();const auto alias=in.utf();in.take(8); // Original entry creation date is not a trust expiry.
    if(!in.valid||(tag!=1&&tag!=2)){return invalid("Saved HTTPS JKS entry is malformed");}
    for(const char16_t c:alias){if(c>127){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Saved HTTPS JKS non-ASCII alias casing requires Java-compatible Unicode support"});}}
    std::uint32_t certificates=1;
    if(tag==1){const auto size=in.number();if(size>MaximumBytes){return invalid("Saved HTTPS JKS key entry exceeds bounds");}in.take(size);certificates=in.number();if(certificates>64){return invalid("Saved HTTPS JKS chain exceeds bounds");}}
    QSslCertificate first;
    for(std::uint32_t i=0;i<certificates;++i){
      if(version==2){const auto type=in.utf();if(type!=u"X.509"&&type!=u"X509"&&type!=u"x.509"&&type!=u"x509"){return invalid("Saved HTTPS JKS certificate type is unsupported");}}
      const auto size=in.number();if(!size||size>MaximumCertificateBytes){return invalid("Saved HTTPS JKS certificate exceeds bounds");}
      const auto der=in.take(size);if(!in.valid){return invalid("Saved HTTPS JKS certificate is truncated");}
      const QByteArray encoded(der.data(),static_cast<qsizetype>(der.size()));const auto parsed=QSslCertificate::fromData(encoded,QSsl::Der);
      if(parsed.size()!=1||parsed.front().isNull()||parsed.front().toDer()!=encoded){return invalid("Saved HTTPS JKS requires one exact DER X.509 certificate");}
      if(i==0){first=parsed.front();}
    }
    if(!in.valid){return invalid("Saved HTTPS JKS entry is truncated");}
    // JavaKeyStore uses the LAST entry of an exact alias. TrustStoreUtil uses
    // trusted entries and only the FIRST certificate of a private-key chain.
    entries.insert_or_assign(alias,std::move(first));
  }
  if(!in.valid||in.position!=body.size()){return invalid("Saved HTTPS JKS has truncated or trailing data");}
  QList<QSslCertificate> anchors;
  // engineLoad keeps literal keys, while JKS getters lower-case aliases with
  // Locale.ENGLISH. Do not trust a raw uppercase key that Java cannot retrieve.
  for(const auto& entry:entries){auto alias=entry.first;for(auto& c:alias){if(c>=u'A'&&c<=u'Z'){c+=u'a'-u'A';}}
    const auto found=entries.find(alias);if(found!=entries.end()&&!found->second.isNull()&&!anchors.contains(found->second)){anchors.push_back(found->second);}}
  return anchors;
}
inline bool sameFile(const struct stat& a,const struct stat& b){
  return S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size
      &&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&b.st_nlink==1
      &&b.st_uid==::geteuid()&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
inline std::string stamp(const struct stat& s){
  return std::to_string(s.st_dev)+":"+std::to_string(s.st_ino)+":"+std::to_string(s.st_size)+":"+std::to_string(s.st_mode)+":"
      +std::to_string(s.st_uid)+":"+std::to_string(s.st_gid)+":"+std::to_string(s.st_nlink)+":"+std::to_string(s.st_mtim.tv_sec)+":"
      +std::to_string(s.st_mtim.tv_nsec)+":"+std::to_string(s.st_ctim.tv_sec)+":"+std::to_string(s.st_ctim.tv_nsec);
}
} // namespace http_trust
struct LibraryHttpTrust {
  std::string scope;
  // Absent means current system-default trust. Present (including empty) is
  // the profile's complete saved trust store, never appended globally.
  std::optional<QList<QSslCertificate>> anchors;
};
inline std::expected<LibraryHttpTrust,storage::Error> libraryHttpTrust(const storage::ProfileLease& lease){
  const auto error=[](storage::ErrorCode code,const char* text){return std::unexpected(storage::Error{code,text});};
  struct stat profile{},afterProfile{},named{},opened{},after{};
  if(!lease.valid()||::fstat(lease.directoryHandle(),&profile)!=0||!S_ISDIR(profile.st_mode)||profile.st_uid!=::geteuid()){
    return error(storage::ErrorCode::UnsafePath,"Saved HTTPS trust requires a held owned profile");}
  const auto prefix="jks-profile:"+std::to_string(profile.st_dev)+":"+std::to_string(profile.st_ino)+":";
  storage::detail::Descriptor file(::openat(lease.directoryHandle(),".certs",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  if(file.get()<0){
    const auto reason=errno;if(reason!=ENOENT){return error(reason==ELOOP?storage::ErrorCode::UnsafePath:storage::ErrorCode::Io,"Cannot open saved HTTPS .certs safely");}
    if(::fstatat(lease.directoryHandle(),".certs",&named,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return error(storage::ErrorCode::Conflict,"Saved HTTPS trust pathname changed");}
    return LibraryHttpTrust{prefix+"absent",std::nullopt};
  }
  if(::fstat(file.get(),&opened)!=0||::fstatat(lease.directoryHandle(),".certs",&named,AT_SYMLINK_NOFOLLOW)!=0||!http_trust::sameFile(opened,named)){
    return error(storage::ErrorCode::UnsafePath,"Saved HTTPS trust must be one owned regular named leaf");}
  if(opened.st_size<0||static_cast<std::uint64_t>(opened.st_size)>http_trust::MaximumBytes){return error(storage::ErrorCode::InvalidFormat,"Saved HTTPS trust store exceeds bounds");}
  std::string bytes(static_cast<std::size_t>(opened.st_size),'\0');std::size_t offset=0;
  while(offset<bytes.size()){const auto got=::read(file.get(),bytes.data()+offset,bytes.size()-offset);if(got<0&&errno==EINTR){continue;}if(got<=0){return error(storage::ErrorCode::Io,"Cannot read complete saved HTTPS trust store");}offset+=static_cast<std::size_t>(got);}
  auto anchors=http_trust::decode(bytes);if(!anchors){return std::unexpected(anchors.error());}
  if(::fstat(file.get(),&after)!=0||::fstatat(lease.directoryHandle(),".certs",&named,AT_SYMLINK_NOFOLLOW)!=0
      ||!http_trust::sameFile(opened,after)||!http_trust::sameFile(after,named)
      ||::fstat(lease.directoryHandle(),&afterProfile)!=0||profile.st_dev!=afterProfile.st_dev||profile.st_ino!=afterProfile.st_ino){
    return error(storage::ErrorCode::Conflict,"Saved HTTPS trust changed while being read");}
  const auto digest=QCryptographicHash::hash(QByteArrayView(bytes.data(),static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha256);
  if(digest.size()!=32){return error(storage::ErrorCode::Io,"Saved HTTPS trust digest is unavailable");}
  return LibraryHttpTrust{prefix+http_trust::stamp(opened)+":"+digest.toHex().toStdString(),std::move(*anchors)};
}
} // namespace biglybtc::engine::detail
