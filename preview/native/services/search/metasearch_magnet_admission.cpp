// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_magnet_admission.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <unistd.h>
namespace biglybtc::search {
namespace {
using Error=storage::Error;using Code=storage::ErrorCode;
std::unexpected<Error> failure(Code code,const char* text){return std::unexpected(Error{code,text});}
bool profileCurrent(const storage::ProfileLease& lease,const std::filesystem::path& profile){
  std::error_code error;if(!lease.valid()||!profile.is_absolute()||std::filesystem::canonical(profile,error)!=profile||error){return false;}
  const int fd=::open(profile.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0){return false;}struct stat a{},b{};
  const bool ok=::fstat(fd,&a)==0&&::fstat(lease.directoryHandle(),&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_uid==::geteuid();::close(fd);return ok;
}
auto policyValues(const engine::NetworkPolicy& p){
  return std::tie(p.mode,p.incomingTcp,p.outgoingTcp,p.incomingUtp,p.outgoingUtp,p.httpTrackers,p.udpTrackers,p.dht,p.pex,p.lsd,p.webSeeds,
      p.upnp,p.natpmp,p.ipv6,p.requireEncryption,p.bindAddresses,p.listenPort,p.enforceBind,p.checkBindOnStart,
      p.proxy.kind,p.proxy.host,p.proxy.username,p.proxy.password,p.proxy.port,p.proxy.peers,p.proxy.trackers,p.proxy.remoteDns,
      p.uploadBytesPerSecond,p.downloadBytesPerSecond,p.seedingUploadBytesPerSecond,p.seedingOnlyUpload,p.lanRates,p.lanUploadBytesPerSecond,p.lanDownloadBytesPerSecond,
      p.socks4Capability,p.mainlineDirectory,p.mainlineRestoreIdentity,p.mainlinePort,p.mainlineIpv4,p.mainlineIpv6,p.mainlineBackupOnly,p.mainlinePeerBootstrapOnly,
      p.mainlineOriginalBindPlan,p.mainlineOriginalAutostart4,p.mainlineOriginalAutostart6,p.mainlineMultihoming4,p.mainlineMultihoming6);
}
std::expected<void,Error> policyCurrent(const storage::ProfileLease& lease,const engine::NetworkPolicy& supplied){
  // This first source-bound action supports the existing direct Public owner.
  // Re-read actual saved settings so an old Host policy is never permission.
  if(supplied.mode!=engine::NetworkMode::Normal||supplied.proxy.kind!=engine::NetworkProxyKind::None){return failure(Code::Unsupported,"Source-bound magnets require the current direct Public policy");}
  auto file=storage::ConfigFile::load(lease,"biglybt.config");
  if(!file&&file.error().code!=Code::Missing){return std::unexpected(file.error());}
  if(file&&file->recoverySource()!=storage::RecoverySource::Primary){return failure(Code::Conflict,"Resolve saved network settings before magnet review");}
  auto actual=engine::readNetworkPolicy(file?&file->document():nullptr,{.mode=engine::NetworkMode::Normal,.socks4Capability=supplied.socks4Capability});
  if(!actual||policyValues(*actual)!=policyValues(supplied)){return failure(Code::Conflict,"Saved network settings differ from the live Public owner");}return {};
}
std::string hashText(const engine::PendingMagnet& intent){
  constexpr char hex[]="0123456789abcdef";std::string out;
  const auto add=[&](const char* bytes,std::size_t length){for(std::size_t i=0;i<length;++i){const auto c=static_cast<unsigned char>(bytes[i]);out+=hex[c>>4];out+=hex[c&15];}};
  if(intent.requested.has_v1()){out="btih:";add(intent.requested.v1.data(),20);}
  if(intent.requested.has_v2()){if(!out.empty()){out+=' ';}out+="btmh:1220";add(intent.requested.v2.data(),32);}return out;
}
engine::PendingMagnetCommitResult rejected(Error error){return {engine::PendingMagnetCommitState::Rejected,std::nullopt,std::move(error)};}
} // namespace
struct MetasearchMagnetAdmission::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile;std::uint64_t session;
  std::optional<SavedMetasearchMagnetHandle> handle;
  engine::PendingMagnet intent;MetasearchMagnetReview review;
  std::thread::id worker=std::this_thread::get_id();bool consumed=false;
};
MetasearchMagnetAdmission::MetasearchMagnetAdmission(std::unique_ptr<Impl> value)noexcept:impl_(std::move(value)){}
MetasearchMagnetAdmission::~MetasearchMagnetAdmission()=default;
const MetasearchMagnetReview& MetasearchMagnetAdmission::description()const noexcept{return impl_->review;}
const core::TorrentHash& MetasearchMagnetAdmission::hash()const noexcept{return impl_->intent.hash;}
std::expected<std::unique_ptr<MetasearchMagnetAdmission>,Error> MetasearchMagnetAdmission::prepare(
    const storage::ProfileLease& caller,const std::filesystem::path& profile,std::uint64_t session,
    const SavedMetasearchMagnetHandle& handle,const engine::NetworkPolicy& policy,std::stop_token receiver){try{
  if(!receiver.stop_possible()||receiver!=handle.receiverToken()||receiver.stop_requested()){return failure(Code::Conflict,"Magnet download receiver changed or cancelled");}
  if(!profileCurrent(caller,profile)){return failure(Code::UnsafePath,"Magnet review requires its actual canonical profile");}
  auto current=handle.current(caller,session);if(!current){return failure(Code::Conflict,"Search result is no longer current");}
  auto direct=policyCurrent(caller,policy);if(!direct){return std::unexpected(direct.error());}
  // Pure existing identity/source parsing; no destination or I/O permission.
  auto intent=engine::inspectPublicMagnet(handle.uri(),policy);
  if(!intent){return std::unexpected(intent.error());}
  if(const auto& expected=handle.expectedV1Hash();expected){
    if(!intent->requested.has_v1()||!std::equal(expected->begin(),expected->end(),intent->requested.v1.data(),[](std::byte a,char b){return std::to_integer<unsigned char>(a)==static_cast<unsigned char>(b);})){return failure(Code::Conflict,"Mapped result hash differs from the actual magnet identity");}
  }
  auto retained=handle.retainedCopy(caller,session);if(!retained){return failure(Code::Conflict,"Search result changed during magnet review");}
  auto lease=caller.retain();if(!lease){return std::unexpected(lease.error());}
  auto value=std::make_unique<Impl>();value->lease=std::move(*lease);value->profile=profile;value->session=session;value->handle.emplace(std::move(*retained));value->intent=std::move(*intent);
  const auto& description=handle.description();value->review={description.key,description.engine,description.query,description.name.empty()?value->intent.displayName:description.name,hashText(value->intent)};
  return std::unique_ptr<MetasearchMagnetAdmission>(new MetasearchMagnetAdmission(std::move(value)));
}catch(const std::bad_alloc&){return failure(Code::Io,"Cannot allocate bounded source-bound magnet review");}}
engine::PendingMagnetCommitResult MetasearchMagnetAdmission::confirm(storage::ProfileLease& caller,std::uint64_t session,
    const std::filesystem::path& destination,const engine::NetworkPolicy& policy,std::stop_token dispatch){try{
  if(!impl_||impl_->worker!=std::this_thread::get_id()||impl_->consumed||!impl_->handle||!impl_->lease||session!=impl_->session||!caller.sharesOwnership(*impl_->lease)||!profileCurrent(caller,impl_->profile)){
    return rejected({Code::Conflict,"Magnet review is stale, consumed or on another profile worker"});}
  if(dispatch.stop_requested()){return rejected({Code::Conflict,"Magnet confirmation cancelled before publication"});}
  auto current=impl_->handle->current(caller,session);if(!current){return rejected({Code::Conflict,"Search result changed before magnet confirmation"});}
  auto direct=policyCurrent(caller,policy);if(!direct){return rejected(direct.error());}
  auto intent=engine::parseMagnetIntent(impl_->handle->uri(),destination,true,policy,engine::MagnetNetwork::Public);if(!intent){return rejected(intent.error());}
  // Preparation does not create or inspect a destination. Confirm authenticates
  // its actual existing directory without following symlinks before any save.
  auto folder=storage::detail::Descriptor(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
  if(folder.get()<0){return rejected({Code::Io,"Cannot inspect reviewed destination"});}
  for(const auto& part:destination.relative_path()){
    storage::detail::Descriptor next(::openat(folder.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));if(next.get()<0){return rejected({Code::UnsafePath,"Reviewed destination is missing, symbolic or inaccessible"});}folder=std::move(next);
  }
  current=impl_->handle->current(caller,session);if(!current){return rejected({Code::Conflict,"Search source changed before pending save"});}
  engine::PendingMagnetCommitBoundary boundary(impl_->lease,*current,impl_->handle->queryToken(),impl_->handle->receiverToken(),dispatch);
  auto result=engine::queueSourceBoundPendingMagnet(caller,*intent,policy,boundary);
  if(result.state!=engine::PendingMagnetCommitState::Rejected){impl_->consumed=true;impl_->handle.reset();impl_->lease.reset();}
  return result;
}catch(const std::bad_alloc&){return rejected({Code::Io,"Cannot prepare bounded magnet confirmation"});}}
} // namespace biglybtc::search
