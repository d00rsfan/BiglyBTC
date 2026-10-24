#include "file_operation_io.hpp"
#include <openssl/evp.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/fs.h>
#include <memory>
#include <sys/syscall.h>
#include <unistd.h>

namespace biglybtc::storage::fileop {
namespace fs=std::filesystem;
[[noreturn]] void fail(ErrorCode code,std::string text){throw Failure{{code,std::move(text)}};}
void require(bool ok,ErrorCode code,const char* text){if(!ok){fail(code,text);}}
void sync(int fd){require(::fsync(fd)==0,ErrorCode::Io,"Cannot sync file operation");}
bool identity(const struct stat&a,const struct stat&b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
bool unchanged(const struct stat&a,const struct stat&b){return identity(a,b)&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
void pathShape(const fs::path& path){
  require(path.is_absolute()&&path.lexically_normal()==path&&path.native().size()<=4096&&path.native().find('\0')==std::string::npos&&path.native().find('\n')==std::string::npos,ErrorCode::UnsafePath,"File operation requires absolute normalized paths without newlines");
  for(const auto& part:path.relative_path()){require(!part.empty()&&part!="."&&part!=".."&&part.native().size()<=255,ErrorCode::UnsafePath,"Unsafe path component");}
}
Fd directory(const fs::path& path,bool create,bool missing){
  pathShape(path);Fd current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));require(current.get()>=0,ErrorCode::Io,"Cannot open filesystem root");
  for(const auto& part:path.relative_path()){
    int next=::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(next<0&&errno==ENOENT){
      if(!create&&missing){return Fd(-1);}require(create,ErrorCode::Missing,"Required parent directory is absent");struct stat parent{};
      require(::fstat(current.get(),&parent)==0&&parent.st_uid==::geteuid(),ErrorCode::UnsafePath,"Cannot create file-operation directory in an unowned parent");
      require(::mkdirat(current.get(),part.c_str(),0700)==0,ErrorCode::Conflict,"File-operation directory appeared or cannot be created");sync(current.get());
      next=::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    }
    require(next>=0,ErrorCode::UnsafePath,"Parent is inaccessible or symbolic");Fd opened(next);struct stat held{},named{};
    require(::fstat(opened.get(),&held)==0&&::fstatat(current.get(),part.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&S_ISDIR(named.st_mode)&&identity(held,named)&&held.st_nlink,ErrorCode::Conflict,"Parent directory identity changed");current=std::move(opened);
  }return current;
}
std::string profilePath(const ProfileLease& lease){
  require(lease.valid(),ErrorCode::Busy,"File operation requires held profile lease");std::array<char,4097> buffer{};const auto name="/proc/self/fd/"+std::to_string(lease.directoryHandle());const auto size=::readlink(name.c_str(),buffer.data(),buffer.size());
  require(size>0&&static_cast<std::size_t>(size)<buffer.size(),ErrorCode::UnsafePath,"Cannot resolve held profile path");std::string path(buffer.data(),static_cast<std::size_t>(size));auto actual=directory(path);struct stat held{},named{};
  require(::fstat(lease.directoryHandle(),&held)==0&&::fstat(actual.get(),&named)==0&&identity(held,named)&&held.st_uid==::geteuid(),ErrorCode::Conflict,"Held profile path identity changed");return path;
}
std::optional<struct stat> inspect(const fs::path& path){
  pathShape(path);auto parent=directory(path.parent_path(),false,true);if(parent.get()<0){return {};}
  struct stat s{};if(::fstatat(parent.get(),path.filename().c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT,ErrorCode::Io,"Cannot inspect file-operation pathname");return {};}
  require(S_ISREG(s.st_mode)&&s.st_uid==::geteuid()&&s.st_nlink>=1&&s.st_nlink<=2&&s.st_size>=0,ErrorCode::UnsafePath,"File-operation leaf must be owned regular data with bounded links");return s;
}
Raw read(const fs::path& path,std::size_t maximum){
  const auto named=inspect(path);if(!named){return {};}
  require(named->st_nlink==1&&static_cast<std::uint64_t>(named->st_size)<=maximum,ErrorCode::UnsafePath,"File-operation document is linked or oversized");auto parent=directory(path.parent_path());Fd file(::openat(parent.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat before{};
  require(file.get()>=0&&::fstat(file.get(),&before)==0&&unchanged(*named,before),ErrorCode::Conflict,"File-operation document changed before open");Raw out{std::string(static_cast<std::size_t>(before.st_size),'\0'),before};std::size_t pos=0;
  while(pos<out.bytes->size()){const auto n=::read(file.get(),out.bytes->data()+pos,out.bytes->size()-pos);if(n<0&&errno==EINTR){continue;}require(n>0,ErrorCode::Io,"Incomplete file-operation document read");pos+=static_cast<std::size_t>(n);}
  struct stat after{};const auto now=inspect(path);require(::fstat(file.get(),&after)==0&&now&&unchanged(before,after)&&unchanged(before,*now),ErrorCode::Conflict,"Document changed during read");return out;
}
void check(const fs::path& path,const Raw& before,std::size_t maximum){auto now=read(path,maximum);require(now.bytes==before.bytes&&(!before.stamp||(now.stamp&&unchanged(*before.stamp,*now.stamp))),ErrorCode::Conflict,"Document changed after preparation");}
void publish(const fs::path& path,const Raw& before,std::string_view data,std::size_t maximum){
  require(data.size()<=maximum,ErrorCode::InvalidFormat,"File-operation document exceeds bound");check(path,before,maximum);auto parent=directory(path.parent_path());const auto temporary=path.filename().string()+".BiglyBTC.fileop-saving";Fd file(::openat(parent.get(),temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
  require(file.get()>=0,ErrorCode::Conflict,"A file-operation temporary already exists or cannot be created");std::size_t pos=0;while(pos<data.size()){auto n=::write(file.get(),data.data()+pos,data.size()-pos);if(n<0&&errno==EINTR){continue;}require(n>0,ErrorCode::Io,"Cannot write file-operation document");pos+=static_cast<std::size_t>(n);}sync(file.get());
  struct stat held{},named{};require(::fstat(file.get(),&held)==0&&::fstatat(parent.get(),temporary.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&unchanged(held,named)&&held.st_uid==::geteuid()&&held.st_nlink==1,ErrorCode::Conflict,"Document temporary identity changed");check(path,before,maximum);
  const int rc=before.bytes?::renameat(parent.get(),temporary.c_str(),parent.get(),path.filename().c_str()):static_cast<int>(::syscall(SYS_renameat2,parent.get(),temporary.c_str(),parent.get(),path.filename().c_str(),RENAME_NOREPLACE));require(rc==0,ErrorCode::Conflict,"Cannot publish file-operation document without conflict");sync(parent.get());
}
void erase(const fs::path& path,const struct stat& expected){
  const auto now=inspect(path);if(!now){return;}require(identity(*now,expected)&&now->st_size==expected.st_size&&now->st_mtim.tv_sec==expected.st_mtim.tv_sec&&now->st_mtim.tv_nsec==expected.st_mtim.tv_nsec,ErrorCode::Conflict,"Removal target identity/content metadata changed");auto parent=directory(path.parent_path());struct stat named{};require(::fstatat(parent.get(),path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&identity(named,*now)&&named.st_uid==::geteuid()&&S_ISREG(named.st_mode),ErrorCode::Conflict,"Removal name changed before unlink");require(::unlinkat(parent.get(),path.filename().c_str(),0)==0,ErrorCode::Io,"Cannot unlink exact file-operation leaf");sync(parent.get());
}
namespace {
using Md=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>;
Md start(){Md md(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(md&&EVP_DigestInit_ex(md.get(),EVP_sha256(),nullptr)==1,ErrorCode::Io,"Cannot initialize file-operation digest");return md;}
std::string end(Md& md){std::array<unsigned char,EVP_MAX_MD_SIZE> raw{};unsigned size=0;require(EVP_DigestFinal_ex(md.get(),raw.data(),&size)==1&&size==32,ErrorCode::Io,"Cannot finish file-operation digest");constexpr char h[]="0123456789abcdef";std::string out(64,'0');for(std::size_t i=0;i<32;++i){out[i*2]=h[raw[i]>>4];out[i*2+1]=h[raw[i]&15];}return out;}
}
std::string digest(std::string_view bytes){auto md=start();require(EVP_DigestUpdate(md.get(),bytes.data(),bytes.size())==1,ErrorCode::Io,"Cannot hash file-operation scope");return end(md);}
std::string digestFile(const fs::path& path,const struct stat& expected,std::stop_token stop){
  const auto before=inspect(path);require(before&&identity(*before,expected)&&before->st_size==expected.st_size&&before->st_mtim.tv_sec==expected.st_mtim.tv_sec&&before->st_mtim.tv_nsec==expected.st_mtim.tv_nsec,ErrorCode::Conflict,"Payload changed before hashing");auto parent=directory(path.parent_path());Fd file(::openat(parent.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat opened{};require(file.get()>=0&&::fstat(file.get(),&opened)==0&&unchanged(*before,opened),ErrorCode::Conflict,"Payload changed before open");auto md=start();std::array<char,65536> bytes{};std::uint64_t total=0;
  for(;;){require(!stop.stop_requested(),ErrorCode::Cancelled,"File operation cancelled before journal");auto n=::read(file.get(),bytes.data(),bytes.size());if(n<0&&errno==EINTR){continue;}require(n>=0,ErrorCode::Io,"Cannot read payload for file operation");if(!n){break;}total+=static_cast<std::uint64_t>(n);require(total<=static_cast<std::uint64_t>(expected.st_size),ErrorCode::Conflict,"Payload grew during hash");require(EVP_DigestUpdate(md.get(),bytes.data(),static_cast<std::size_t>(n))==1,ErrorCode::Io,"Cannot hash payload");}
  const auto after=inspect(path);struct stat fdAfter{};require(after&&::fstat(file.get(),&fdAfter)==0&&unchanged(opened,*after)&&unchanged(opened,fdAfter)&&total==static_cast<std::uint64_t>(expected.st_size),ErrorCode::Conflict,"Payload changed while hashing");return end(md);
}
}
