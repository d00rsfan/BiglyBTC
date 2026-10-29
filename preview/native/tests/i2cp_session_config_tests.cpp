// SPDX-License-Identifier: GPL-2.0-or-later
#include "session.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
using i2p::Bytes;using i2p::View;
namespace {
void require(bool condition,const char* message){if(!condition){throw std::runtime_error(message);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
Bytes read(const std::filesystem::path& path,std::size_t maximum=cp::MaximumFrameBytes){
  std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input){throw std::runtime_error("Cannot open synthetic I2CP fixture: "+path.string());}
  const auto size=static_cast<std::streamoff>(input.tellg());require(size>=0&&static_cast<std::uint64_t>(size)<=maximum,"I2CP fixture exceeds bound");
  Bytes out(static_cast<std::size_t>(size));input.seekg(0);if(!out.empty()){input.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(out.size()));}
  if(!input){throw std::runtime_error("Cannot read complete synthetic I2CP fixture: "+path.string());}return out;
}
i2p::ClientIdentity identity(const std::filesystem::path& fixtures,const std::string& name){
  const auto dest=read(fixtures/(name+".sender"),1024),key=read(fixtures/(name+".known-test-key"),32);
  return take(i2p::makeClientIdentity(dest,Bytes(256,0x51),key));
}
cp::SessionOptions utf8Options(){return {{"z","last"},{"\xee\x80\x80","BMP"},{"\xf0\x90\x80\x80","supplementary"},{"inbound.nickname","\xd1\x82\xd1\x83\xd0\xbd\xd0\xbd\xd0\xb5\xd0\xbb\xd1\x8c"},{"a",std::string("\0=;\n",4)}};}
cp::SessionOptions maximumOptions(){
  cp::SessionOptions out{{"A",std::string(252,'v')}};
  for(unsigned i=0;i<127;++i){std::string key(255,'b');key[252]=static_cast<char>('0'+i/100);key[253]=static_cast<char>('0'+(i/10)%10);key[254]=static_cast<char>('0'+i%10);out.emplace_back(std::move(key),std::string(255,'v'));}return out;
}
void originalsAndTampering(const std::filesystem::path& fixtures){
  constexpr std::uint64_t date=1761739200123;
  for(const std::string family:{"dsa","ed25519"}){
    auto signer=identity(fixtures,family);
    for(const auto& options:{cp::SessionOptions{},utf8Options(),maximumOptions()}){
      auto config=take(cp::signSessionConfig(signer,options,date));const auto bytes=take(cp::encodeSessionConfig(config));
      auto parsed=take(cp::decodeSessionConfig(bytes));require(cp::verifySessionConfig(parsed,date).has_value(),"Signed session failed verification");
      require(take(cp::encodeSessionConfig(parsed))==bytes,"Config decode/write changed original bytes");
      require(parsed.destination.wire==signer.destination()->wire&&parsed.options==take(cp::canonicalSessionOptions(options)),"Destination/options changed");
      require(config.signature.size()==(family=="dsa"?40U:64U),"Wrong original raw signature width");
      for(const auto delta:{-180000LL,180000LL}){require(cp::verifySessionConfig(parsed,static_cast<std::uint64_t>(static_cast<std::int64_t>(date)+delta)).has_value(),"Inclusive original date boundary refused");}
      for(const auto delta:{-180001LL,180001LL}){require(!cp::verifySessionConfig(parsed,static_cast<std::uint64_t>(static_cast<std::int64_t>(date)+delta)),"Stale/future session accepted");}
      auto changed=parsed;changed.destination.wire[0]^=1;require(!cp::verifySessionConfigSignature(changed),"Session signature did not cover opaque Destination bytes");
      changed=parsed;changed.signature[0]^=1;require(!cp::verifySessionConfigSignature(changed),"Changed signature accepted");
      changed=parsed;++changed.creationMilliseconds;require(!cp::verifySessionConfigSignature(changed),"Changed date accepted");
      changed=parsed;changed.options.emplace_back("unknown.future","preserved");require(!cp::verifySessionConfigSignature(changed),"Changed options accepted");
      for(const std::uint16_t id:{0,65534,65535}){
        auto frame=take(cp::decodeFrame(take(cp::encodeSessionMessage(cp::ReconfigureSession{id,parsed}))));
        auto reconfigured=std::get<cp::ReconfigureSession>(take(cp::decodeSessionMessage(frame)));
        require(reconfigured.sessionId==id&&cp::verifySessionConfigSignature(reconfigured.config).has_value(),"Reconfigure ID/config changed");
        frame.payload[1]^=1;auto altered=std::get<cp::ReconfigureSession>(take(cp::decodeSessionMessage(frame)));
        require(altered.sessionId!=id&&cp::verifySessionConfigSignature(altered.config).has_value(),"Reconfigure ID was wrongly claimed as signed");
      }
      const auto created=take(cp::decodeSessionMessage(take(cp::decodeFrame(take(cp::encodeSessionMessage(cp::CreateSession{parsed}))))));
      require(cp::verifySessionConfigSignature(std::get<cp::CreateSession>(created).config).has_value(),"Create wrapper lost signature");
      auto extra=bytes;extra.push_back(0);require(!cp::decodeSessionConfig(extra),"Trailing config accepted");
      auto truncated=bytes;truncated.pop_back();require(!cp::decodeSessionConfig(truncated),"Truncated config accepted");
    }
    auto invalid=cp::signSessionConfig(signer,{},0);require(!invalid,"Null creation date accepted");
    require(!cp::signSessionConfig(signer,{},std::uint64_t(1)<<63),"Wrapped Java date accepted");
    const auto maximumDate=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto extreme=take(cp::signSessionConfig(signer,{},maximumDate));require(!cp::verifySessionConfig(extreme,maximumDate),"Java signed clock-window overflow gained freshness");
    const Bytes data{0,1,2,0xff};const auto signature=take(i2p::signData(signer.destination()->signingType,signer.signingPrivateKey(),data));
    require(i2p::verifyData(signer.destination()->signingType,signer.destination()->signingPublicKey,signature,data).has_value(),"Shared raw signer/verifier failed");
    require(!i2p::signData(signer.destination()->signingType,signer.signingPrivateKey(),Bytes(i2p::MaximumSignedDataBytes+1)),"Shared signer input unbounded");
    require(!i2p::verifyData(signer.destination()->signingType,{},signature,data),"Invalid public key width accepted");
    require(!i2p::verifyData(static_cast<i2p::SigningType>(8),signer.destination()->signingPublicKey,signature,data),"Unsupported signing algorithm accepted");
    std::cout<<"PASS signed original SessionConfig/Create/Reconfigure "<<family<<'\n';
  }
}
void optionRules(const std::filesystem::path& fixtures){
  auto ordered=take(cp::canonicalSessionOptions(utf8Options()));
  require(ordered[ordered.size()-2].first=="\xf0\x90\x80\x80"&&ordered.back().first=="\xee\x80\x80","UTF-8 byte sort replaced original UTF-16 ordering");
  require(!cp::canonicalSessionOptions({{"a","1"},{"a","2"}}),"Duplicate key accepted");
  require(!cp::canonicalSessionOptions({{std::string(256,'a'),""}}),"Overlong key accepted");
  require(!cp::canonicalSessionOptions({{"a",std::string(256,'v')}}),"Overlong value accepted");
  require(!cp::canonicalSessionOptions({{"a","\xc0\x80"}}),"Overlong UTF-8 accepted");
  require(!cp::canonicalSessionOptions({{"a","\xed\xa0\x80"}}),"UTF-8 surrogate accepted");
  auto max=maximumOptions();require(cp::canonicalSessionOptions(max).has_value(),"Original65535 properties body refused");max[0].second.push_back('v');require(!cp::canonicalSessionOptions(max),"Original properties size overflow accepted");
  for(const auto* key:{"i2cp.username","i2cp.password","i2cp.leaseSetOfflineExpiration","i2cp.leaseSetTransientPublicKey","i2cp.leaseSetOfflineSignature"}){
    const auto unsupported=cp::canonicalSessionOptions({{key,"x"}});require(!unsupported&&unsupported.error().code==i2p::ErrorCode::Unsupported,"Unsupported auth/offline option silently accepted");
  }
  const auto projected=take(cp::projectRouterOptions({{"i2cp.tcp.host","must not reach router"},{"i2cp.gzip","true"},{"i2p.reseedURL",std::string(2048,'x')},{"oversize",std::string(256,'x')},{"i2cp.domainSocket","not in original removal list"},{"unknown.future","yes"}}));
  require(projected==cp::SessionOptions({{"i2cp.domainSocket","not in original removal list"},{"unknown.future","yes"}}),"Original client-only/long option projection changed");
  require(!cp::projectRouterOptions({{"inbound.nickname",std::string(128,'\xc3')+std::string(128,'\xa9')}}),"Invalid multibyte property accepted");
  std::string encodedLong;for(unsigned i=0;i<128;++i){encodedLong+="\xc3\xa9";}
  require(!cp::projectRouterOptions({{"inbound.nickname",encodedLong}}),"UTF-8 byte overflow was silently dropped as a long Java string");
  const auto merged=take(cp::mergeSessionOptions({{"keep","old"},{"change","old"}},{{"change","new"},{"added","new"}}));
  require(merged==cp::SessionOptions({{"added","new"},{"change","new"},{"keep","old"}}),"Reconfigure omitted key was removed");
  // Original readProperties(false) accepts unique unsorted entries, while
  // verifySignature reserializes them in canonical Java order.
  auto signer=identity(fixtures,"ed25519");auto cfg=take(cp::signSessionConfig(signer,{{"a","1"},{"b","2"}},1761739200123));
  auto raw=take(cp::encodeSessionConfig(cfg));const auto start=cfg.destination.wire.size()+2;
  std::rotate(raw.begin()+static_cast<std::ptrdiff_t>(start),raw.begin()+static_cast<std::ptrdiff_t>(start+6),raw.begin()+static_cast<std::ptrdiff_t>(start+12));
  auto parsed=take(cp::decodeSessionConfig(raw));require(cp::verifySessionConfigSignature(parsed).has_value(),"Unsorted unique original properties verify with wrong preimage");
  require(take(cp::encodeSessionConfig(parsed))==take(cp::encodeSessionConfig(cfg)),"Canonical original reserialization changed");
}
// Fixed-name offline fixture bridge only; no profile/clock/session/network.
class Descriptor {
public:
  explicit Descriptor(int value):value_(value){}
  ~Descriptor(){if(value_>=0){::close(value_);}}
  Descriptor(const Descriptor&)=delete;Descriptor& operator=(const Descriptor&)=delete;
  int get()const{return value_;}
private:int value_;
};
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
Bytes ownedRead(int root,const std::string& leaf){
  Descriptor fd(::openat(root,leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat before{},after{},named{};
  require(fd.get()>=0&&::fstat(fd.get(),&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==::getuid()&&before.st_nlink==1&&before.st_size>=0&&before.st_size<=static_cast<off_t>(cp::MaximumFrameBytes),"Unsafe/missing offline session input");
  Bytes bytes(static_cast<std::size_t>(before.st_size));std::size_t at=0;
  while(at<bytes.size()){const auto count=::read(fd.get(),bytes.data()+at,bytes.size()-at);if(count<0&&errno==EINTR){continue;}require(count>0,"Incomplete offline session input");at+=static_cast<std::size_t>(count);}
  i2p::Byte extra=0;require(::read(fd.get(),&extra,1)==0&&::fstat(fd.get(),&after)==0&&::fstatat(root,leaf.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&same(before,after)&&same(before,named),"Offline session input changed");return bytes;
}
void ownedWrite(int root,const std::string& leaf,View bytes){
  require(bytes.size()<=cp::MaximumFrameBytes,"Unbounded offline session output");Descriptor fd(::openat(root,leaf.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
  require(fd.get()>=0,"Offline session output exists/cannot create");std::size_t at=0;
  while(at<bytes.size()){const auto count=::write(fd.get(),bytes.data()+at,bytes.size()-at);if(count<0&&errno==EINTR){continue;}require(count>0,"Incomplete offline session output");at+=static_cast<std::size_t>(count);}
  require(::fsync(fd.get())==0,"Cannot sync offline session output");
}
void javaInterop(const std::filesystem::path& path){
  require(path.is_absolute(),"Offline session directory must be absolute");Descriptor root(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat bound{};
  require(root.get()>=0&&::fstat(root.get(),&bound)==0&&bound.st_uid==::getuid()&&(bound.st_mode&0777)==0700,"Offline session directory must be private/owned");
  const auto clock=ownedRead(root.get(),"creation.clock");require(clock.size()==8,"Invalid original clock width");std::uint64_t date=0;for(auto byte:clock){date=(date<<8)|byte;}
  require(date>600000&&date<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),"Invalid original fixture date range");
  const std::array<const char*,6> labels{"dsa-empty","dsa-utf8","dsa-maximum","ed25519-empty","ed25519-utf8","ed25519-maximum"};
  unsigned ordinal=0;
  for(const auto* text:labels){
    const std::string label(text),family=label.starts_with("dsa-")?"dsa":"ed25519";
    const auto destination=ownedRead(root.get(),family+".sender"),key=ownedRead(root.get(),family+".sign");auto signer=take(i2p::makeClientIdentity(destination,Bytes(256,0x51),key));
    const auto options=label.ends_with("-empty")?cp::SessionOptions{}:label.ends_with("-utf8")?utf8Options():maximumOptions();
    for(const std::string kind:{"create","reconfigure"}){
      const auto original=ownedRead(root.get(),label+".java."+kind);const auto frame=take(cp::decodeFrame(original));auto message=take(cp::decodeSessionMessage(frame));
      const auto& cfg=std::visit([](const auto& value)->const cp::SessionConfig&{return value.config;},message);
      require(cfg.destination.wire==destination&&cfg.creationMilliseconds==date&&cfg.options==take(cp::canonicalSessionOptions(options)),"Java session fields differ");
      require(cp::verifySessionConfig(cfg,date).has_value(),"Java original session signature/date refused");
      require(take(cp::sessionConfigPreimage(cfg))==ownedRead(root.get(),label+".java.preimage"),"Original Java canonical signature preimage differs");
      if(kind=="create"){require(std::holds_alternative<cp::CreateSession>(message),"Java create has wrong type");}
      else{const auto expectedId=static_cast<std::uint16_t>(ordinal==0?0U:ordinal==5?65534U:ordinal);require(std::holds_alternative<cp::ReconfigureSession>(message)&&std::get<cp::ReconfigureSession>(message).sessionId==expectedId,"Java reconfigure ID differs");}
      auto native=take(cp::signSessionConfig(signer,cfg.options,date));
      cp::SessionMessage outgoing=kind=="create"?cp::SessionMessage(cp::CreateSession{native}):cp::SessionMessage(cp::ReconfigureSession{std::get<cp::ReconfigureSession>(message).sessionId,native});
      auto wire=take(cp::encodeSessionMessage(outgoing));ownedWrite(root.get(),label+".native."+kind,wire);
      if(ordinal==0&&kind=="create"){
        wire.back()^=1;ownedWrite(root.get(),"native.bad-signature.create",wire);
        auto stale=take(cp::signSessionConfig(signer,cfg.options,date-600000));
        require(cp::verifySessionConfigSignature(stale).has_value()&&!cp::verifySessionConfig(stale,date),"Native stale-date refusal lost");
        ownedWrite(root.get(),"native.stale.create",take(cp::encodeSessionMessage(cp::CreateSession{stale})));
      }
      std::cout<<"PASS NATIVE VERIFIED JAVA "<<label<<' '<<kind<<'\n';
    }
    ++ordinal;
  }
  auto auth=take(cp::decodeFrame(ownedRead(root.get(),"java.unsupported-auth.create")));const auto refused=cp::decodeSessionMessage(auth);
  require(!refused&&refused.error().code==i2p::ErrorCode::Unsupported,"Original auth options silently accepted");
  require(::fsync(root.get())==0,"Cannot sync offline session directory");std::cout<<"PASS NATIVE REFUSED ORIGINAL AUTH OPTIONS\n";
}
} // namespace
int main(int argc,char** argv){
  try{if(argc==3&&std::string_view(argv[1])=="--java-interop"){javaInterop(std::filesystem::path(argv[2]));return 0;}
    require(argc==2,"Usage: i2cp-session-config-tests SYNTHETIC_FIXTURE_DIR | --java-interop PRIVATE_DIR");const std::filesystem::path fixtures(argv[1]);originalsAndTampering(fixtures);optionRules(fixtures);std::cout<<"PASS finite signed I2CP session codec\n";return 0;}
  catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
