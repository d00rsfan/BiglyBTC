#include "scheduler_io.hpp"
#include <openssl/evp.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <sys/syscall.h>
#include <unistd.h>

namespace biglybtc::storage::scheduler_detail {
[[noreturn]] void fail(ErrorCode code,const char* message) { throw Failure{{code,message}}; }
[[noreturn]] void io(const char* message) {
  if(errno==ELOOP||errno==ENOTDIR){fail(ErrorCode::UnsafePath,message);}
  fail(ErrorCode::Io,message);
}
void checkStop(std::stop_token stop) { if(stop.stop_requested()){fail(ErrorCode::Cancelled,"Scheduler operation cancelled before mutation");} }
bool same(const struct stat& a,const struct stat& b) noexcept {
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink
      &&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
Raw read(int parent,const char* name,std::size_t maximum) {
  detail::Descriptor fd(::openat(parent,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  if(fd.get()<0){if(errno==ENOENT){return {};}io("Cannot open scheduler file without following links");}
  Raw out;
  if(::fstat(fd.get(),&out.identity)!=0){io("Cannot inspect scheduler file");}
  const auto& info=out.identity;
  if(!S_ISREG(info.st_mode)||info.st_uid!=::geteuid()||info.st_nlink!=1||info.st_size<0){fail(ErrorCode::UnsafePath,"Scheduler file must be an owned regular single-link file");}
  if(static_cast<std::uint64_t>(info.st_size)>maximum){fail(ErrorCode::InvalidFormat,"Scheduler file exceeds its byte bound");}
  out.bytes.emplace(static_cast<std::size_t>(info.st_size),'\0');
  std::size_t pos=0;
  while(pos<out.bytes->size()){
    const auto n=::read(fd.get(),out.bytes->data()+pos,out.bytes->size()-pos);
    if(n<0&&errno==EINTR){continue;}if(n<0){io("Cannot read scheduler file");}
    if(!n){fail(ErrorCode::Conflict,"Scheduler file shortened during read");}pos+=static_cast<std::size_t>(n);
  }
  char extra=0;ssize_t n=0;do{n=::read(fd.get(),&extra,1);}while(n<0&&errno==EINTR);
  if(n<0){io("Cannot finish scheduler read");}if(n){fail(ErrorCode::Conflict,"Scheduler file grew during read");}
  struct stat after{},named{};
  if(::fstat(fd.get(),&after)!=0||::fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)!=0){io("Cannot recheck scheduler file identity");}
  if(!same(info,after)||!same(info,named)){fail(ErrorCode::Conflict,"Scheduler file changed during read");}
  return out;
}
void unchanged(int parent,const char* name,const Raw& before,std::size_t maximum) {
  const auto now=read(parent,name,maximum);
  if(now.bytes!=before.bytes||(before.bytes&&!same(before.identity,now.identity))){fail(ErrorCode::Conflict,"Scheduler source changed; reload before editing");}
}
detail::Descriptor directory(int parent,const char* name,bool missingAllowed) {
  detail::Descriptor fd(::openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
  if(fd.get()<0){if(missingAllowed&&errno==ENOENT){return fd;}io("Cannot open scheduler directory without following links");}
  struct stat st{};if(::fstat(fd.get(),&st)!=0){io("Cannot inspect scheduler directory");}
  if(!S_ISDIR(st.st_mode)||st.st_uid!=::geteuid()||(st.st_mode&0022)!=0||!st.st_nlink){fail(ErrorCode::UnsafePath,"Scheduler directory is not owned and live");}
  directoryIdentity(parent,name,fd.get());return fd;
}
void directoryIdentity(int parent,const char* name,int fd) {
  struct stat held{},named{};
  if(::fstat(fd,&held)!=0||::fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)!=0){io("Cannot verify scheduler directory identity");}
  if(!S_ISDIR(named.st_mode)||held.st_dev!=named.st_dev||held.st_ino!=named.st_ino||held.st_uid!=named.st_uid||!named.st_nlink){fail(ErrorCode::Conflict,"Scheduler directory was replaced");}
}
Raw replace(int parent,const char* name,const Raw& before,std::string_view value,std::size_t maximum) {
  if(value.size()>maximum){fail(ErrorCode::InvalidFormat,"Scheduler output exceeds byte bound");}
  unchanged(parent,name,before,maximum);
  const auto temporary=std::string(name)+".BiglyBTC.saving";
  detail::Descriptor fd(::openat(parent,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));
  if(fd.get()<0){if(errno==EEXIST){fail(ErrorCode::Conflict,"Scheduler temporary exists; no blind recovery or overwrite");}io("Cannot create scheduler temporary");}
  struct stat created{};if(::fstat(fd.get(),&created)!=0){io("Cannot inspect scheduler temporary");}
  std::size_t pos=0;
  while(pos<value.size()){
    const auto n=::write(fd.get(),value.data()+pos,value.size()-pos);
    if(n<0&&errno==EINTR){continue;}if(n<=0){io("Cannot write scheduler temporary");}pos+=static_cast<std::size_t>(n);
  }
  if(::fsync(fd.get())!=0){io("Cannot sync scheduler temporary");}
  struct stat synced{},named{};
  if(::fstat(fd.get(),&synced)!=0||::fstatat(parent,temporary.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){io("Cannot verify scheduler temporary");}
  if(!S_ISREG(synced.st_mode)||synced.st_dev!=created.st_dev||synced.st_ino!=created.st_ino||synced.st_nlink!=1
      ||synced.st_size<0||static_cast<std::uint64_t>(synced.st_size)!=value.size()||!same(synced,named)){
    fail(ErrorCode::Conflict,"Scheduler temporary identity changed");}
  unchanged(parent,name,before,maximum);
  if(before.bytes){if(::renameat(parent,temporary.c_str(),parent,name)!=0){io("Cannot publish scheduler update");}}
  else {
    constexpr unsigned NoReplace=1;
    if(::syscall(SYS_renameat2,parent,temporary.c_str(),parent,name,NoReplace)!=0){
      if(errno==EEXIST){fail(ErrorCode::Conflict,"Scheduler primary appeared before first publication");}io("Cannot publish new scheduler file");}
  }
  if(::fsync(parent)!=0){io("Scheduler publication happened but parent sync failed; reload required");}
  return read(parent,name,maximum);
}
void remove(int parent,const char* name,const Raw& before,std::size_t maximum) {
  unchanged(parent,name,before,maximum);if(!before.bytes){return;}
  if(::unlinkat(parent,name,0)!=0){io("Cannot remove scheduler intent");}
  if(::fsync(parent)!=0){io("Scheduler intent removal sync failed");}
}
std::string sha256(int fd,std::uint64_t expected) {
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!md||EVP_DigestInit_ex(md.get(),EVP_sha256(),nullptr)!=1){fail(ErrorCode::Io,"Cannot initialize scheduler artifact authentication");}
  std::array<unsigned char,65536> bytes{};std::uint64_t count=0;
  for(;;){const auto n=::read(fd,bytes.data(),bytes.size());if(n<0&&errno==EINTR){continue;}if(n<0){io("Cannot read scheduler artifact");}if(!n){break;}
    const auto size=static_cast<std::uint64_t>(n);if(size>expected-count){fail(ErrorCode::Conflict,"Scheduler artifact grew during authentication");}
    count+=size;if(EVP_DigestUpdate(md.get(),bytes.data(),static_cast<std::size_t>(n))!=1){fail(ErrorCode::Io,"Cannot hash scheduler artifact");}}
  if(count!=expected){fail(ErrorCode::Conflict,"Scheduler artifact length differs from approval");}
  std::array<unsigned char,EVP_MAX_MD_SIZE> value{};unsigned size=0;
  if(EVP_DigestFinal_ex(md.get(),value.data(),&size)!=1||size!=32){fail(ErrorCode::Io,"Cannot finish scheduler artifact authentication");}
  constexpr char hex[]="0123456789abcdef";std::string out(64,'0');for(std::size_t i=0;i<32;++i){out[i*2]=hex[value[i]>>4];out[i*2+1]=hex[value[i]&15];}return out;
}
}
