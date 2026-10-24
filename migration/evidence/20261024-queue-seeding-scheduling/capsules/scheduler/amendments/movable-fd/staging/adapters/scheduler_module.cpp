#include "scheduler_service.hpp"
#include "scheduler_io.hpp"
#include <cctype>
#include <fcntl.h>
#include <unistd.h>

namespace biglybtc::storage {
std::expected<std::unique_ptr<SchedulerService>,Error> loadApprovedSchedulerService(
    const ProfileLease& lease,const std::filesystem::path& path,const SchedulerModuleApproval& approval,std::stop_token stop) {
  using namespace scheduler_detail;
  try{
    checkStop(stop);if(!lease.valid()){fail(ErrorCode::Busy,"Scheduler module requires held profile lease");}
    if(approval.sha256.size()!=64||approval.bytes==0||approval.bytes>64*1024*1024||approval.version.empty()||approval.version.size()>64){fail(ErrorCode::InvalidFormat,"Invalid compiled scheduler module approval");}
    for(const char c:approval.sha256){if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){fail(ErrorCode::InvalidFormat,"Scheduler SHA256 approval must be lowercase hexadecimal");}}
    unsigned dots=0,digits=0;for(const char c:approval.version){if(c=='.'){if(!digits||++dots>3){fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}digits=0;}else if(c>='0'&&c<='9'){++digits;}else{fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}}
    if(!dots||!digits){fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}
    if(!path.is_absolute()||path.lexically_normal()!=path||path.native().size()>4096||path.native().find('\0')!=std::string::npos||path.filename().empty()){
      fail(ErrorCode::UnsafePath,"Scheduler module path must be explicit, absolute and normalized");}
    scheduler_detail::Descriptor parent(::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(parent.get()<0){io("Cannot open scheduler module path root");}
    for(const auto& component:path.parent_path().relative_path()){
      if(component.empty()||component=="."||component==".."){fail(ErrorCode::UnsafePath,"Unsafe scheduler module parent");}
      scheduler_detail::Descriptor next(::openat(parent.get(),component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
      if(next.get()<0){io("Cannot traverse approved scheduler module parent");}parent=std::move(next);
    }
    const auto leaf=path.filename().string();scheduler_detail::Descriptor file(::openat(parent.get(),leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    if(file.get()<0){io("Cannot open approved scheduler module");}struct stat before{},after{},named{};
    if(::fstat(file.get(),&before)!=0){io("Cannot inspect scheduler module");}
    if(!S_ISREG(before.st_mode)||before.st_uid!=::geteuid()||before.st_nlink!=1||(before.st_mode&0022)!=0||before.st_size<=0||static_cast<std::uint64_t>(before.st_size)!=approval.bytes){
      fail(ErrorCode::UnsafePath,"Scheduler module is not the safe approved artifact shape");}
    if(sha256(file.get(),approval.bytes)!=approval.sha256){fail(ErrorCode::Conflict,"Scheduler module bytes differ from compiled approval");}
    if(::fstat(file.get(),&after)!=0||::fstatat(parent.get(),leaf.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){io("Cannot recheck scheduler module identity");}
    if(!same(before,after)||!same(before,named)){fail(ErrorCode::Conflict,"Scheduler module changed during authentication");}
    checkStop(stop);
    const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1),BT_PLUGIN_ABI_V1,-1,0,"1.8",nullptr,nullptr};
    auto module=plugins::Module::loadDescriptor(file.get(),"SpeedScheduler",BT_CAP_SCHEDULER,options,approval.version);
    if(!module){return std::unexpected(Error{ErrorCode::Unsupported,module.error().message});}
    return SchedulerService::open(lease,std::move(*module),stop);
  }catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory to authenticate scheduler module"});}
  catch(const std::filesystem::filesystem_error&){return std::unexpected(Error{ErrorCode::UnsafePath,"Invalid scheduler module path"});}
}
}
