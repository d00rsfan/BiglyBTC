#include "original_file_io.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace {
void need(bool v,const char* why){if(!v){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& v){if(!v){throw std::runtime_error(v.error().message);}return std::move(*v);}
void done(std::expected<void,st::Error>&& v){if(!v){throw std::runtime_error(v.error().message);}}
struct Fd{int value=-1;explicit Fd(int n):value(n){}~Fd(){if(value>=0){::close(value);}}};
std::optional<std::string> load(const fs::path& path,std::size_t bound=16*1024*1024){Fd f(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));if(f.value<0&&errno==ENOENT){return {};}struct stat s{};need(f.value>=0&&::fstat(f.value,&s)==0&&S_ISREG(s.st_mode)&&s.st_uid==::geteuid()&&s.st_nlink==1&&s.st_size>=0&&static_cast<std::uint64_t>(s.st_size)<=bound,"Unsafe generated input");std::string bytes(static_cast<std::size_t>(s.st_size),'\0');std::size_t n=0;while(n<bytes.size()){auto count=::read(f.value,bytes.data()+n,bytes.size()-n);if(count<0&&errno==EINTR){continue;}need(count>0,"Generated input read failed");n+=static_cast<std::size_t>(count);}return bytes;}
void fresh(const fs::path& path,std::string_view bytes){Fd f(::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));need(f.value>=0,"Generated output already exists");std::size_t n=0;while(n<bytes.size()){const auto count=::write(f.value,bytes.data()+n,bytes.size()-n);if(count<0&&errno==EINTR){continue;}need(count>0,"Generated output write failed");n+=static_cast<std::size_t>(count);}need(::fsync(f.value)==0,"Generated output sync failed");}
std::uint64_t number(const std::string& s){std::uint64_t n=0;auto r=std::from_chars(s.data(),s.data()+s.size(),n);need(r.ec==std::errc{}&&r.ptr==s.data()+s.size(),"Invalid tape unsigned number");return n;}
bool safe(const fs::path& p){if(p.empty()||p.is_absolute()||p.lexically_normal()!=p){return false;}for(const auto& part:p){if(part==".."||part=="."||part.empty()){return false;}}return true;}
std::span<const std::byte> bytes(std::string_view s){return std::as_bytes(std::span(s.data(),s.size()));}
}
int main(int argc,char** argv){try{
  need(argc==3,"Usage: original_file_io_driver OWNED_ROOT REQUEST_TSV");const fs::path root(argv[1]),tapeArg(argv[2]);need(root.is_absolute()&&root.lexically_normal()==root&&fs::canonical(root)==root,"Canonical owned root required");auto relative=root.lexically_relative("/tmp");need(safe(relative)&&relative.begin()->string().starts_with("biglybtc-"),"Only generated /tmp/biglybtc-* roots");struct stat rs{};need(::lstat(root.c_str(),&rs)==0&&S_ISDIR(rs.st_mode)&&rs.st_uid==::geteuid(),"Owned fixture root required");const auto tape=tapeArg.is_absolute()?tapeArg:root/tapeArg;need(tape.parent_path()==root,"Tape must be an owned root leaf");auto text=load(tape);need(text.has_value(),"Tape missing");
  std::optional<st::OriginalFileIo> io;std::optional<st::OriginalFileGeometry> geometry;auto kind=st::OriginalStorageKind::Linear;int dataFd=-1;fs::path control;std::size_t step=0;
  auto close=[&](){if(io){done(io->flush());io.reset();}if(dataFd>=0){::close(dataFd);dataFd=-1;}};
  auto open=[&](){need(geometry.has_value()&&!io,"OPEN geometry missing or duplicate");const auto payload=root/"payload.bin.az!";dataFd=::open(payload.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);need(dataFd>=0,"Cannot open owned payload");control=root/"control"/st::originalControlLeaf(1,kind);std::optional<std::string> original;if(kind!=st::OriginalStorageKind::Linear){original=load(control);}
    st::OriginalControlPublisher publish=[&](const auto& expected,std::string_view next)->std::expected<void,st::Error>{try{const auto dir=control.parent_path();if(!fs::exists(dir)){need(fs::create_directory(dir),"Cannot create owned control directory");fs::permissions(dir,fs::perms::owner_all);}struct stat ds{};need(::lstat(dir.c_str(),&ds)==0&&S_ISDIR(ds.st_mode)&&ds.st_uid==::geteuid(),"Unsafe control directory");need(load(control)==expected,"Control CAS changed");const auto saving=fs::path(control.string()+".native-saving");fresh(saving,next);need(load(control)==expected,"Control CAS changed before publish");need(::rename(saving.c_str(),control.c_str())==0,"Cannot publish generated control");Fd parent(::open(dir.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));need(parent.value>=0&&::fsync(parent.value)==0,"Cannot sync generated control parent");return {};}catch(const std::exception& e){return std::unexpected(st::Error{st::ErrorCode::Io,e.what()});}};
    io.emplace(take(st::OriginalFileIo::open(dataFd,*geometry,kind,std::move(original),std::move(publish))));
  };
  std::istringstream lines(*text);std::string line;
  while(std::getline(lines,line)){
    if(line.empty()||line[0]=='#'){continue;}need(++step<=10000,"Tape exceeds step bound");std::istringstream words(line);std::string command;words>>command;
    if(command=="OPEN"){need(!geometry,"One geometry per tape");std::string k,o,n,p;need(bool(words>>k>>o>>n>>p),"OPEN fields");const auto value=number(k),piece=number(p);need(value>=1&&value<=4&&piece<=std::numeric_limits<std::uint32_t>::max(),"OPEN kind/piece");kind=static_cast<st::OriginalStorageKind>(value);geometry=st::OriginalFileGeometry{number(o),number(n),static_cast<std::uint32_t>(piece)};open();}
    else if(command=="REOPEN"){need(!io,"REOPEN requires CLOSE");open();}
    else if(command=="CLOSE"){close();}
    else{need(io.has_value(),"IO command without open");if(command=="WRITE"||command=="COMPLETE"){std::string position,name;need(bool(words>>position>>name)&&safe(name),"Write/complete fields");const auto data=load(root/name,64*1024*1024);need(data.has_value(),"Tape source missing");if(command=="WRITE"){done(io->write(number(position),bytes(*data)));}else{static_cast<void>(take(io->pieceComplete(number(position),bytes(*data))));}}
      else if(command=="READ"){std::string position,count,label;need(bool(words>>position>>count>>label)&&!label.empty()&&label.size()<=80&&std::ranges::all_of(label,[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}),"READ fields/label");const auto length=number(count);need(length<=64*1024*1024,"READ bound");std::string output(static_cast<std::size_t>(length),'\0');const auto result=take(io->read(number(position),std::as_writable_bytes(std::span(output.data(),output.size()))));fresh(root/("read-"+label+".bin"),output);std::string json="{\"missing\":[";for(std::size_t i=0;i<result.missing.size();++i){if(i){json+=',';}json+="{\"offset\":"+std::to_string(result.missing[i].offset)+",\"length\":"+std::to_string(result.missing[i].length)+'}';}json+="],\"physicalBytes\":"+std::to_string(result.physicalBytes)+",\"length\":"+std::to_string(io->length())+"}\n";fresh(root/("read-"+label+".json"),json);std::cout<<"READ "<<label<<" missing="<<result.missing.size()<<'\n';}
      else if(command=="LENGTH"){std::string n;need(bool(words>>n),"LENGTH field");done(io->setLength(number(n)));}
      else if(command=="FLUSH"){done(io->flush());}
      else{throw std::runtime_error("Unknown tape command");}}
    std::string extra;need(!(words>>extra),"Unexpected tape fields");if(io){struct stat s{};need(::fstat(dataFd,&s)==0,"STATE stat");std::cout<<"STATE "<<step<<" length="<<io->length()<<" physicalBytes="<<s.st_size<<'\n';}
  }
  close();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
