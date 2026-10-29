// SPDX-License-Identifier: GPL-2.0-or-later
#include "messages.hpp"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
namespace i2p=biglybtc::services::i2p;namespace cp=i2p::i2cp;
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> v){if(!v){throw std::runtime_error(v.error().message);}return std::move(*v);}
void controls(){
  for(const auto session:{0U,7U,65534U})for(const std::uint32_t id:{0U,0x80000000U,0xffffffffU})for(const bool begin:{false,true}){
    const auto raw=take(cp::encodeReceiveControl({static_cast<std::uint16_t>(session),id,begin}));require(raw.size()==11&&raw[3]==6&&raw[4]==(begin?6:7),"Original control header differs");auto parsed=take(cp::decodeReceiveControl(take(cp::decodeFrame(raw))));require(parsed.sessionId==session&&parsed.messageId==id&&parsed.begin==begin,"Unsigned control fields differ");auto wrong=raw;wrong.push_back(0);require(!cp::decodeFrame(wrong),"Trailing receive frame accepted");wrong=raw;wrong.pop_back();require(!cp::decodeFrame(wrong),"Truncated receive frame accepted");
  }
  require(!cp::encodeReceiveControl({65535,1,true}),"Unknown receive session accepted");require(!cp::decodeReceiveControl(cp::Frame{6,{0,7,1}}),"Short receive control accepted");require(!cp::decodeReceiveControl(cp::Frame{8,i2p::Bytes(6)}),"Unknown receive type accepted");require(cp::originalFastReceiveEnabled({{"i2cp.fastReceive","TrUe"}}),"Original Boolean true spelling differs");require(!cp::originalFastReceiveEnabled({})&&!cp::originalFastReceiveEnabled({{"i2cp.fastReceive"," true "}}),"Raw router absent/trim behavior invented");
}
struct Fd {int n;explicit Fd(int value):n(value){}~Fd(){if(n>=0){::close(n);}}};
bool stamp(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
i2p::Bytes read(int root,const std::string& leaf){Fd fd(::openat(root,leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat before{},after{},named{};require(fd.n>=0&&::fstat(fd.n,&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&before.st_nlink==1&&before.st_size>=0&&before.st_size<=70000,"Unsafe/unbounded vector");i2p::Bytes out(static_cast<std::size_t>(before.st_size));std::size_t at=0;while(at<out.size()){auto n=::read(fd.n,out.data()+at,out.size()-at);if(n<0&&errno==EINTR){continue;}require(n>0,"Cannot read vector");at+=static_cast<std::size_t>(n);}require(::fstat(fd.n,&after)==0&&::fstatat(root,leaf.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&stamp(before,after)&&stamp(after,named),"Vector changed");return out;}
void write(int root,const std::string& leaf,i2p::View bytes){Fd fd(::openat(root,leaf.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(fd.n>=0&&bytes.size()<=70000,"Cannot exclusively create bounded vector");std::size_t at=0;while(at<bytes.size()){auto n=::write(fd.n,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR){continue;}require(n>0,"Cannot write vector");at+=static_cast<std::size_t>(n);}require(::fsync(fd.n)==0,"Cannot sync vector");}
void interop(const std::filesystem::path& path){
  require(path.is_absolute(),"Absolute root required");Fd root(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat st{};require(root.n>=0&&::fstat(root.n,&st)==0&&st.st_uid==::geteuid()&&(st.st_mode&077)==0,"Private owned root required");std::uint32_t index=0;const i2p::Bytes expected{0,0xff,0x51,3};
  for(const std::string family:{"dsa","ed25519"})for(const bool claimed:{false,true}){
    const auto label=family+"-"+(claimed?"3":"2");const auto id=0x80000000U+index++;const auto from=read(root.n,family+".sender"),secret=read(root.n,family+".known-test-key");const auto to=take(i2p::decodeDestination(read(root.n,family+".recipient")));
    for(const bool begin:{true,false}){const auto suffix=begin?"begin":"end";const auto raw=read(root.n,label+".java."+suffix);auto parsed=take(cp::decodeReceiveControl(take(cp::decodeFrame(raw))));require(parsed.sessionId==7&&parsed.messageId==id&&parsed.begin==begin,"Java control fields differ");const auto encoded=take(cp::encodeReceiveControl(parsed));require(encoded==raw,"Exact original control bytes differ");write(root.n,label+".native."+suffix,encoded);}
    const auto available=take(cp::decodeMessageStatus(take(cp::decodeFrame(read(root.n,label+".java.available")))));const auto payload=take(cp::decodeMessagePayload(take(cp::decodeFrame(read(root.n,label+".java.payload")))));require(available.sessionId==7&&available.messageId==id&&available.code==0&&available.nonce==1&&available.size==payload.payload.size()&&payload.sessionId==7&&payload.messageId==id,"Actual Available/Payload fields differ");const auto mux=take(cp::decodeMuxDatagram(payload.payload));require(mux.fromPort==13&&mux.toPort==42&&mux.protocol==(claimed?i2p::Datagram3Protocol:i2p::Datagram2Protocol),"Actual original mux differs");
    if(claimed){const auto body=take(i2p::decode3(mux.datagram));require(body.claimedSender==take(i2p::decodeDestination(from)).hash&&body.payload==expected,"Original claimed payload differs");}else{const auto body=take(i2p::decode2(mux.datagram,to.hash));require(body.sender.wire==from&&body.payload==expected,"Original verified payload differs");}
    auto packet=take(claimed?i2p::encode3(from,expected):i2p::encode2(from,secret,to.hash,expected));auto gzip=take(cp::encodeMuxDatagram({claimed?i2p::Datagram3Protocol:i2p::Datagram2Protocol,13,42,std::move(packet)},cp::GzipMode::Deflate));write(root.n,label+".native.available",take(cp::encodeMessageStatus({7,id,0,static_cast<std::uint32_t>(gzip.size()),1})));write(root.n,label+".native.payload",take(cp::encodeMessagePayload({7,id,std::move(gzip)})));std::cout<<"PASS NATIVE VERIFIED JAVA "<<label<<" receive\n";
  }
  require(::fsync(root.n)==0,"Cannot sync vector directory");
}
int main(int argc,char** argv){try{if(argc==3&&std::string_view(argv[1])=="--java-interop"){interop(argv[2]);return 0;}require(argc==1,"Usage: i2cp-receive-control-tests [--java-interop ROOT]");controls();std::cout<<"PASS bounded original receive controls\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
