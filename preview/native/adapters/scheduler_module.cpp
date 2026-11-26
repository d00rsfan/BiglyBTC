#include "scheduler_service.hpp"
#include "scheduler_io.hpp"
#ifdef _WIN32
#include "../platform/windows_relative_file.hpp"
#endif

namespace biglybtc::storage {
namespace {
constexpr bt_plugin_options_v1 SchedulerOptions{
    sizeof(bt_plugin_options_v1), BT_PLUGIN_ABI_V1, -1, 0, "1.8", nullptr, nullptr};
} // namespace

std::expected<std::unique_ptr<SchedulerService>,Error> loadApprovedSchedulerService(
    const ProfileLease& lease,const std::filesystem::path& path,const SchedulerModuleApproval& approval,std::stop_token stop) {
  using namespace scheduler_detail;
  try{
    checkStop(stop);if(!lease.valid()){fail(ErrorCode::Busy,"Scheduler module requires held profile lease");}
    if(approval.sha256.size()!=64||approval.bytes==0||approval.bytes>64*1024*1024||approval.version.empty()||approval.version.size()>64){fail(ErrorCode::InvalidFormat,"Invalid compiled scheduler module approval");}
    for(const char c:approval.sha256){if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){fail(ErrorCode::InvalidFormat,"Scheduler SHA256 approval must be lowercase hexadecimal");}}
    unsigned dots=0,digits=0;for(const char c:approval.version){if(c=='.'){if(!digits||++dots>3){fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}digits=0;}else if(c>='0'&&c<='9'){++digits;}else{fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}}
    if(!dots||!digits){fail(ErrorCode::InvalidFormat,"Invalid scheduler module version");}
    if(!path.is_absolute()||path.lexically_normal()!=path||path.native().size()>4096||path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos||path.filename().empty()){
      fail(ErrorCode::UnsafePath,"Scheduler module path must be explicit, absolute and normalized");}
    const auto parent = detail::openStatePathDirectory(path.parent_path());
    if (!parent) { throw Failure{parent.error()}; }
    const auto leaf = path.filename();
#ifdef _WIN32
    namespace win = platform::detail;
    const auto opened = win::openRelativeFile(parent->get(), leaf.native(), FILE_GENERIC_READ,
        FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile);
    if (!opened) { fail(ErrorCode::Io, "Cannot open approved scheduler module"); }
    const StateDescriptor file(*opened);
    constexpr bool installedOwner = true;
#else
    auto selected = detail::openStateFile(parent->get(), leaf);
    if (!selected) { throw Failure{selected.error()}; }
    const auto file = std::move(*selected);
    constexpr bool installedOwner = false;
#endif
    const auto before = detail::stateStamp(file.get());
    if (!before || !before->regular || (!before->owned && !installedOwner) || before->links != 1
        || before->size <= 0 || static_cast<std::uint64_t>(before->size) != approval.bytes) {
      fail(ErrorCode::UnsafePath, "Scheduler module is not the safe approved artifact shape");
    }
#ifndef _WIN32
    if (before->mode & 0022) { fail(ErrorCode::UnsafePath, "Scheduler module was writable by another user"); }
#endif
    if (sha256(file.get(), approval.bytes) != approval.sha256) {
      fail(ErrorCode::Conflict, "Scheduler module bytes differ from compiled approval");
    }
    const auto after = detail::stateStamp(file.get());
    const auto named = detail::stateStampAt(parent->get(), leaf);
    if (!after || !named || !*named || !detail::sameStateSnapshot(*before, *after)
        || !detail::sameStateSnapshot(*before, **named)) {
      fail(ErrorCode::Conflict, "Scheduler module changed during authentication");
    }
    checkStop(stop);
    auto module=plugins::Module::loadDescriptor(file.get(),"SpeedScheduler",BT_CAP_SCHEDULER,SchedulerOptions,approval.version,installedOwner);
    if(!module){return std::unexpected(Error{ErrorCode::Unsupported,module.error().message});}
    return SchedulerService::open(lease,std::move(*module),stop);
  }catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory to authenticate scheduler module"});}
  catch(const std::filesystem::filesystem_error&){return std::unexpected(Error{ErrorCode::UnsafePath,"Invalid scheduler module path"});}
}

std::expected<std::unique_ptr<SchedulerService>,Error> loadBuiltinSchedulerService(
    const ProfileLease& lease, bt_plugin_entry_fn entry, std::stop_token stop) {
  if (stop.stop_requested()) {
    return std::unexpected(Error{ErrorCode::Cancelled, "Scheduler operation cancelled before mutation"});
  }
  if (!lease.valid()) {
    return std::unexpected(Error{ErrorCode::Busy, "Scheduler module requires held profile lease"});
  }
  auto module = plugins::Module::loadBuiltin(entry, "SpeedScheduler", BT_CAP_SCHEDULER,
      SchedulerOptions, "0.1.0");
  if (!module) { return std::unexpected(Error{ErrorCode::Unsupported, module.error().message}); }
  return SchedulerService::open(lease, std::move(*module), stop);
}
}
