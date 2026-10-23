#include "saved_speed_manager_view.hpp"
#include "file_io.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
Error cancelled(){return {ErrorCode::Cancelled,"Saved speed estimate read cancelled"};}
Error format(){return {ErrorCode::InvalidFormat,"Invalid or excessive saved speed estimate data"};}
bool sameDirectory(const struct stat& a,const struct stat& b){return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
bool sameFile(const struct stat& a,const struct stat& b){return S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
}
std::expected<SavedSpeedManagerView,Error> readSavedSpeedManagerView(const ProfileLease& lease,std::string_view settingsBytes,std::stop_token stop){try{
  if(stop.stop_requested()){return std::unexpected(cancelled());}if(!lease.valid()){return std::unexpected(Error{ErrorCode::UnsafePath,"Held profile lease required for saved speed estimates"});}
  auto settings=core::readSavedSpeedSettings(settingsBytes);if(!settings){return std::unexpected(settings.error()==core::SavedSpeedError::Memory?Error{ErrorCode::Io,"Insufficient memory for saved speed settings"}:format());}
  if(stop.stop_requested()){return std::unexpected(cancelled());}if(!settings->historyLeaf){return std::unexpected(Error{ErrorCode::Unsupported,"Saved AS identity has no supported safe mapper filename"});}
  SavedSpeedManagerView result;result.settings=std::move(*settings);const int root=lease.directoryHandle();struct stat before{};
  if(::fstat(root,&before)!=0||!S_ISDIR(before.st_mode)){return std::unexpected(Error{ErrorCode::UnsafePath,"Held speed-estimate profile is not a directory"});}
  detail::Descriptor net(::openat(root,"net",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));const auto failure=errno;
  if(stop.stop_requested()){return std::unexpected(cancelled());}if(net.get()<0){if(failure==ENOENT){return result;}return std::unexpected(Error{failure==ELOOP||failure==ENOTDIR?ErrorCode::UnsafePath:ErrorCode::Io,"Saved network-estimate directory cannot be opened safely"});}
  struct stat opened{},named{};if(::fstat(net.get(),&opened)!=0||::fstatat(root,"net",&named,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(opened,named)||opened.st_uid!=::geteuid()||(opened.st_mode&0022)){return std::unexpected(Error{ErrorCode::UnsafePath,"Saved network-estimate directory is unsafe or changed"});}
  bool anyPresent=false,primaryMissing=false;
  const auto finish=[&]()->std::expected<SavedSpeedManagerView,Error>{struct stat after{};
    if(!lease.valid()||lease.directoryHandle()!=root||::fstat(root,&after)!=0||!sameDirectory(before,after)||::fstatat(root,"net",&after,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(opened,after)){return std::unexpected(Error{ErrorCode::Conflict,"Saved network-estimate directory changed while reading"});}
    if(stop.stop_requested()){return std::unexpected(cancelled());}return std::move(result);};
  for(unsigned index=0;index<3;++index){
    const std::string suffix=index==0?"":index==1?(primaryMissing?".saving":".bak"):(primaryMissing?".bak":".saving");const auto leaf=*result.settings.historyLeaf+suffix;
    if(stop.stop_requested()){return std::unexpected(cancelled());}struct stat old{};const bool exists=::fstatat(net.get(),leaf.c_str(),&old,AT_SYMLINK_NOFOLLOW)==0;
    if(!exists){if(errno!=ENOENT){return std::unexpected(Error{ErrorCode::Io,"Cannot inspect saved speed estimate candidate"});}if(index==0){primaryMissing=true;}continue;}
    anyPresent=true;if(!S_ISREG(old.st_mode)||old.st_uid!=::geteuid()||old.st_nlink!=1||(old.st_mode&0022)){return std::unexpected(Error{ErrorCode::UnsafePath,"Saved speed estimate must be an owned regular file"});}
    auto bytes=detail::readFile(net.get(),leaf,core::Limits{}.inputBytes);if(stop.stop_requested()){return std::unexpected(cancelled());}if(!bytes){return std::unexpected(bytes.error());}if(!*bytes){return std::unexpected(Error{ErrorCode::Conflict,"Saved speed estimate disappeared while reading"});}
    auto parsed=core::readSavedSpeedHistory(**bytes);struct stat after{};if(::fstatat(net.get(),leaf.c_str(),&after,AT_SYMLINK_NOFOLLOW)!=0||!sameFile(old,after)){return std::unexpected(Error{ErrorCode::Conflict,"Saved speed estimate changed while reading"});}
    if(stop.stop_requested()){return std::unexpected(cancelled());}if(!parsed){if(parsed.error()==core::SavedSpeedError::Memory){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for saved speed estimates"});}continue;}
    result.history=std::move(*parsed);result.recoverySource=index==0?RecoverySource::Primary:suffix==".bak"?RecoverySource::Backup:RecoverySource::Saving;return finish();
  }
  if(anyPresent){return std::unexpected(format());}return finish();
 }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for saved speed estimates"});}}
} // namespace biglybtc::storage
