#include "linear_to_compact.hpp"
#include "file_operations.hpp"
#include "torrent_state.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <cerrno>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace compat=biglybtc::compat;
void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
template<class T>T take(std::expected<T,st::Error> result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
bool inside(const fs::path& root,const fs::path& path){auto current=path.begin();for(auto part=root.begin();part!=root.end();++part,++current){if(current==path.end()||*current!=*part){return false;}}return current!=path.end();}
void owned(const fs::path& root,const fs::path& path){struct stat info{};
  require(path.is_absolute()&&fs::canonical(path)==path&&inside(root,path)&&::lstat(path.c_str(),&info)==0&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid(),"Canonical owned generated directory required");}
std::string jsonString(std::string_view value){std::string out="\"";constexpr char hex[]="0123456789abcdef";
  for(const unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else{out+=static_cast<char>(c);}}return out+'"';}
void report(const fs::path& path,std::string_view bytes){const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"Fresh result file required");
  std::size_t used=0;while(used<bytes.size()){const auto count=::write(fd,bytes.data()+used,bytes.size()-used);if(count<0&&errno==EINTR){continue;}if(count<=0){::close(fd);throw std::runtime_error("Cannot write handoff result");}used+=static_cast<std::size_t>(count);}const auto okay=::fsync(fd)==0;::close(fd);require(okay,"Cannot sync result");}
}
int main(int argc,char** argv){
  if(argc!=4){std::cerr<<"Usage: linear_to_compact_handoff_driver WORK_ROOT PROFILE FRESH_OUTPUT\n";return 1;}
  try{
    const fs::path root(argv[1]),profile(argv[2]),output(argv[3]);struct stat rootInfo{};
    require(root.parent_path()==fs::path("/tmp")&&root.filename().string().starts_with("biglybtc-managed-handoff-storage-compaction-")
        &&fs::canonical(root)==root&&::lstat(root.c_str(),&rootInfo)==0&&S_ISDIR(rootInfo.st_mode)&&rootInfo.st_uid==::geteuid()&&(rootInfo.st_mode&077)==0,"Owned private generated work root required");
    owned(root,profile);owned(root,output.parent_path());
    require(output.is_absolute()&&output.lexically_normal()==output&&inside(root,output)&&!fs::exists(output)&&!fs::is_symlink(fs::symlink_status(output)),"Fresh generated output required");
    auto lease=take(st::ProfileLease::acquire(profile));require(!st::hasPendingOriginalCompaction(lease)&&!st::hasPendingFileOperation(lease),"Fresh stopped handoff cannot contain a pending operation");
    const auto config=take(st::ConfigFile::load(lease,"downloads.config"));const auto library=core::readLibrary(config.document());
    require(library&&library->issues.empty()&&library->downloads.size()==1&&library->downloads[0].hash.has_value(),"One actual original download required");const auto hash=*library->downloads[0].hash;
    const auto store=take(st::TorrentStateStore::open(lease));const auto state=take(store.load(lease,hash));const auto layout=compat::originalLayout(state.document());
    require(layout&&layout->files.size()==3&&layout->pieceLength==16384&&layout->bytes==131072&&layout->files[1].offset==3072&&layout->files[1].bytes==103304&&!layout->files[1].padding,"Bounded original three-file v1 fixture required");
    // No LT session is created. The original process has stopped before this
    // standalone held-lease call; the adapter first accepts explicit Skip.
    const auto result=take(st::compactOriginalFile(lease,hash,1));
    require(!st::hasPendingOriginalCompaction(lease)&&!result.recovered,"Clean compaction did not finish");
    const auto after=take(st::prepareFileOperation(lease,{hash,st::FileOperationKind::RemoveKeepData,{}}));
    const st::FileOperationEntry* target=nullptr;for(const auto& file:after.preview().files){if(file.index==1){target=&file;}}
    require(target&&target->present&&target->actualBytes>=0,"Actual compact payload missing");
    require(fs::create_directory(output),"Cannot allocate result directory");fs::permissions(output,fs::perms::owner_all);
    const auto json="{\n  \"schema\":1,\n  \"status\":\"COMPACTED_ACTUAL_STOPPED_PROFILE\",\n  \"hash\":"+jsonString(hash.hex())
        +",\n  \"index\":1,\n  \"previousPhysicalBytes\":"+std::to_string(result.previousPhysicalBytes)
        +",\n  \"physicalBytes\":"+std::to_string(result.physicalBytes)+",\n  \"payloadPath\":"+jsonString(target->source.string())
        +",\n  \"device\":"+jsonString(std::to_string(target->device))+",\n  \"inode\":"+jsonString(std::to_string(target->inode))
        +",\n  \"pending\":false,\n  \"engineOwnerCreated\":false\n}\n";
    report(output/"result.json",json);std::cout<<"COMPACTION_HANDOFF_STOPPED "<<hash.hex()<<" bytes="<<result.physicalBytes<<'\n';return 0;
  }catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 2;}
}
