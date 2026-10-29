// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaseset.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
i2p::Bytes read(const std::filesystem::path& path){require(std::filesystem::file_size(path)<=1024,"Unbounded synthetic fixture");std::ifstream in(path,std::ios::binary);require(in.good(),"Cannot read bounded synthetic fixture");i2p::Bytes out(std::istreambuf_iterator<char>(in),{});require(!in.bad()&&out.size()<=1024,"Invalid fixture read");return out;}
i2p::ClientIdentity identity(const std::filesystem::path& path,const std::string& family){return take(i2p::makeClientIdentity(read(path/(family+".sender")),i2p::Bytes(256,0x51),read(path/(family+".known-test-key"))));}
cp::LeaseRequest request(std::uint16_t id,std::size_t count,std::uint64_t now){cp::LeaseRequest out;out.sessionId=id;for(std::size_t i=0;i<count;++i){cp::RequestedLease lease;lease.gateway.fill(static_cast<i2p::Byte>(i+1));lease.tunnelId=static_cast<std::uint32_t>(100+i);lease.expiresMilliseconds=now+600001+i*1000;out.leases.push_back(lease);}return out;}
void keyRules(){
  std::array<i2p::Byte,256> x{};x.back()=2;auto fixed=take(cp::LeaseEncryptionKey::fromPrivateKey(x));require(fixed.publicKey().size()==256&&fixed.publicKey().back()==4,"I2P group/generator/private endian mismatch");
  require(std::all_of(fixed.publicKey().begin(),fixed.publicKey().end()-1,[](auto c){return c==0;}),"I2P public width mismatch");
  auto key=take(cp::LeaseEncryptionKey::generate());auto same=take(cp::LeaseEncryptionKey::fromPrivateKey(key.privateKey()));require(std::equal(key.publicKey().begin(),key.publicKey().end(),same.publicKey().begin()),"Generated pair does not derive");
  auto moved=std::move(key);require(!key.valid()&&key.privateKey().empty()&&moved.valid(),"Moved key retained authority");moved.clear();require(!moved.valid()&&moved.privateKey().empty(),"Cleared key remains live");
  x.fill(0);require(!cp::LeaseEncryptionKey::fromPrivateKey(x),"Zero exponent accepted");x.back()=1;require(!cp::LeaseEncryptionKey::fromPrivateKey(x),"One exponent accepted");x.fill(255);require(!cp::LeaseEncryptionKey::fromPrivateKey(x),"Out of range exponent accepted");require(!cp::LeaseEncryptionKey::fromPrivateKey(i2p::View(x).first(255)),"Wrong-width exponent accepted");
}
void messages(const std::filesystem::path& fixtures){
  constexpr std::uint64_t now=1700000000499ULL;
  for(const std::string family:{"dsa","ed25519"}){
    auto signer=identity(fixtures,family);auto key=take(cp::LeaseEncryptionKey::generate());
    for(const std::size_t count:{1U,16U}){
      const auto req=request(7,count,now);auto wire=take(cp::encodeLeaseRequest(req));const auto parsed=take(cp::decodeLeaseRequest(take(cp::decodeFrame(wire))));require(parsed.sessionId==7&&parsed.leases.size()==count&&parsed.leases[0].expiresMilliseconds==req.leases[0].expiresMilliseconds,"Original request milliseconds changed");
      auto ls=take(cp::signLeaseSet2(signer,key,req,cp::LeaseSetPolicy{count==16},now));require(ls.publishedMilliseconds==1700000000000ULL&&ls.expiresMilliseconds==(req.leases.back().expiresMilliseconds/1000)*1000,"Original published rounding/latest floor expiry differs");
      auto next=take(cp::signLeaseSet2(signer,key,req,{},now,now));require(next.publishedMilliseconds==1700000001000ULL,"Monotone original published timestamp differs");
      const auto encoded=take(cp::encodeLeaseSet2(ls));const auto decoded=take(cp::decodeLeaseSet2(encoded));require(decoded.destination.wire==signer.destination()->wire&&decoded.leases.size()==count&&decoded.unpublished==(count==16),"LS2 roundtrip fields differ");
      auto secret=take(cp::encodeCreateLeaseSet2(7,ls,key));const auto copied=secret.bytes();const auto frame=take(cp::decodeFrame(copied));auto reply=take(cp::decodeCreateLeaseSet2(frame));require(reply.sessionId==7&&std::equal(reply.key.privateKey().begin(),reply.key.privateKey().end(),key.privateKey().begin()),"Original private key trailer/pair mismatch");
      auto moved=std::move(secret);require(secret.bytes().empty()&&!moved.bytes().empty(),"Secret frame move retained source");moved.clear();require(moved.bytes().empty(),"Secret frame clear retained bytes");
      auto changed=ls;changed.leases[0].gateway[0]^=1;require(!cp::verifyLeaseSet2(changed),"Changed gateway authenticated");changed=ls;changed.encryptionKey[30]^=1;require(!cp::verifyLeaseSet2(changed),"Changed encryption key authenticated");changed=ls;changed.signature.back()^=1;require(!cp::verifyLeaseSet2(changed),"Changed signature authenticated");changed=ls;changed.unpublished=!changed.unpublished;require(!cp::verifyLeaseSet2(changed),"Changed flags authenticated");
      auto altered=frame;altered.payload.back()^=1;require(!cp::decodeCreateLeaseSet2(altered),"Wrong private key accepted");altered=frame;altered.payload.push_back(0);require(!cp::decodeCreateLeaseSet2(altered),"Trailing data accepted");altered=frame;altered.payload[2]=5;require(!cp::decodeCreateLeaseSet2(altered),"Encrypted/meta LeaseSet accepted");
      auto wrongKey=take(cp::LeaseEncryptionKey::generate());require(!cp::encodeCreateLeaseSet2(7,ls,wrongKey),"Different encryption pair accepted");require(!cp::encodeCreateLeaseSet2(65535,ls,key),"Unknown session accepted");
    }
    auto req=request(7,1,now);req.leases.clear();require(!cp::signLeaseSet2(signer,key,req,{},now),"Zero lease readiness accepted");req=request(7,17,now);require(!cp::encodeLeaseRequest(req),"17 leases accepted");req=request(7,1,now);req.leases[0].expiresMilliseconds=now;require(!cp::signLeaseSet2(signer,key,req,{},now),"Expired lease accepted");req=request(7,1,now);req.leases[0].tunnelId=0;require(!cp::encodeLeaseRequest(req),"Zero tunnel accepted");req=request(7,1,now);require(!cp::signLeaseSet2(signer,key,req,{},0xffffffffULL*1000),"Published overflow accepted");
    auto reqWire=take(cp::encodeLeaseRequest(req));reqWire.pop_back();require(!cp::decodeFrame(reqWire),"Truncated request frame accepted");
    std::cout<<"PASS original standard LS2 key/message/signature rules "<<family<<'\n';
  }
  require(take(cp::leaseSetPolicy({{"i2cp.dontPublishLeaseSet","TrUe"}})).unpublished,"Original boolean spelling differs");require(!take(cp::leaseSetPolicy({{"i2cp.dontPublishLeaseSet"," true "}})).unpublished,"Original boolean trimming invented");
  for(const auto& opts:{cp::SessionOptions{{"i2cp.leaseSetType","5"}},cp::SessionOptions{{"i2cp.leaseSetEncType","4"}},cp::SessionOptions{{"i2cp.leaseSetPrivateKey","secret"}},cp::SessionOptions{{"i2cp.leaseSetOption.0","a=b"}},cp::SessionOptions{{"i2cp.username",""}}}){require(!cp::leaseSetPolicy(opts),"Unsupported policy silently downgraded");}
}
struct Fd {int value=-1;explicit Fd(int n):value(n){}~Fd(){if(value>=0){::close(value);}}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;};
bool stamp(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
i2p::Bytes ownedRead(int root,const char* name){
  Fd fd(::openat(root,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));require(fd.value>=0,"Cannot read fixed offline vector");struct stat before{},after{},named{};require(::fstat(fd.value,&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&before.st_nlink==1&&before.st_size>=0&&before.st_size<=4096,"Unsafe/unbounded offline vector");
  i2p::Bytes out(static_cast<std::size_t>(before.st_size));std::size_t at=0;while(at<out.size()){auto n=::read(fd.value,out.data()+at,out.size()-at);if(n<0&&errno==EINTR){continue;}require(n>0,"Cannot read offline vector bytes");at+=static_cast<std::size_t>(n);}require(::fstat(fd.value,&after)==0&&::fstatat(root,name,&named,AT_SYMLINK_NOFOLLOW)==0&&stamp(before,after)&&stamp(after,named),"Offline vector changed");return out;
}
void ownedWrite(int root,const char* name,i2p::View bytes){Fd fd(::openat(root,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(fd.value>=0&&bytes.size()<=4096,"Cannot exclusively create offline vector");std::size_t at=0;while(at<bytes.size()){auto n=::write(fd.value,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR){continue;}require(n>0,"Cannot write offline vector");at+=static_cast<std::size_t>(n);}require(::fsync(fd.value)==0,"Cannot sync offline vector");}
void interop(const std::filesystem::path& path){
  require(path.is_absolute(),"Offline root must be absolute");Fd root(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat st{};require(root.value>=0&&::fstat(root.value,&st)==0&&st.st_uid==::geteuid()&&(st.st_mode&077)==0,"Private owned offline root required");
  auto clock=ownedRead(root.value,"creation.clock");require(clock.size()==8,"Wrong original clock width");std::uint64_t now=0;for(auto c:clock){now=(now<<8)|c;}require(now>0&&now<0xffffffffULL*1000-1000,"Wrong original clock range");
  const std::array<const char*,4> labels{"dsa-one","dsa-sixteen","ed25519-one","ed25519-sixteen"};
  for(std::size_t row=0;row<labels.size();++row){
    const std::string label=labels[row],family=row<2?"dsa":"ed25519";const auto count=row%2==0?1U:16U;
    auto dest=ownedRead(root.value,(family+".sender").c_str()),signing=ownedRead(root.value,(family+".known-test-key").c_str());auto signer=take(i2p::makeClientIdentity(dest,i2p::Bytes(256,0x51),signing));
    const auto req=take(cp::decodeLeaseRequest(take(cp::decodeFrame(ownedRead(root.value,(label+".java.request").c_str())))));require(req.sessionId==7&&req.leases.size()==count,"Java actual request fields differ");const auto expected=request(7,count,now);for(std::size_t i=0;i<count;++i){require(req.leases[i].gateway==expected.leases[i].gateway&&req.leases[i].tunnelId==expected.leases[i].tunnelId&&req.leases[i].expiresMilliseconds==expected.leases[i].expiresMilliseconds,"Java request exact fields differ");}
    auto actual=take(cp::decodeCreateLeaseSet2(take(cp::decodeFrame(ownedRead(root.value,(label+".java.create").c_str())))));require(actual.sessionId==7&&actual.leaseSet.destination.wire==dest&&actual.leaseSet.leases.size()==count&&actual.leaseSet.publishedMilliseconds==((now+500)/1000)*1000&&actual.leaseSet.unpublished==(count==16),"Java LS2 fields differ");
    for(std::size_t i=0;i<count;++i){require(actual.leaseSet.leases[i].gateway==req.leases[i].gateway&&actual.leaseSet.leases[i].tunnelId==req.leases[i].tunnelId&&actual.leaseSet.leases[i].expiresMilliseconds==(req.leases[i].expiresMilliseconds/1000)*1000,"Java Lease2 second projection differs");}
    auto key=take(cp::LeaseEncryptionKey::generate());auto ls=take(cp::signLeaseSet2(signer,key,req,{count==16},now));auto encoded=take(cp::encodeCreateLeaseSet2(7,ls,key));ownedWrite(root.value,(label+".native.create").c_str(),encoded.bytes());
    if(row==0){auto bad=ls;bad.signature.back()^=1;auto frame=take(cp::decodeFrame(encoded.bytes()));const auto at=3+take(cp::encodeLeaseSet2(ls)).size()-1;frame.payload[at]^=1;auto wire=cp::encodeFrame(frame.type,frame.payload);require(wire.has_value(),"Cannot encode altered fixture");ownedWrite(root.value,"native.bad-signature.create",*wire);require(!cp::verifyLeaseSet2(bad),"Changed native signature accepted");}
    std::cout<<"PASS NATIVE VERIFIED JAVA "<<label<<" request create\n";
  }
  require(::fsync(root.value)==0,"Cannot sync offline vector directory");
}
int main(int argc,char** argv){try{if(argc==3&&std::string_view(argv[1])=="--java-interop"){interop(argv[2]);return 0;}require(argc==2,"Usage: i2cp-leaseset-tests SYNTHETIC_FIXTURES | --java-interop PRIVATE_DIR");keyRules();messages(argv[1]);std::cout<<"PASS bounded original LeaseSet2 prerequisite\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
