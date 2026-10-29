// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTPlugin / DHTTransportUDPImpl, cb8c6cb9; bounded explicit-bind slice.
#include "azureus_public_policy.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
std::unexpected<Error> error(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
struct Config {std::string bytes;struct stat stamp{};core::Document document;Config(std::string b,struct stat s,core::Document d):bytes(std::move(b)),stamp(s),document(std::move(d)){}};
std::expected<Config,Error> config(const ProfileLease& lease){
  struct stat before{},pending{},after{};const auto fd=lease.directoryHandle();
  if(::fstatat(fd,"biglybt.config.saving",&pending,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return error(ErrorCode::Conflict,"Resolve public DHT pending configuration first");}
  if(::fstatat(fd,"biglybt.config",&before,AT_SYMLINK_NOFOLLOW)!=0){return error(ErrorCode::Missing,"Current primary public DHT configuration required");}
  if(!S_ISREG(before.st_mode)||before.st_uid!=::geteuid()||before.st_nlink!=1||(before.st_mode&0022)){return error(ErrorCode::UnsafePath,"Unsafe public DHT configuration");}
  auto source=ConfigFile::load(lease,"biglybt.config");if(!source){return std::unexpected(source.error());}if(source->recoverySource()!=RecoverySource::Primary){return error(ErrorCode::Conflict,"Primary public DHT configuration required");}
  if(::fstatat(fd,"biglybt.config",&after,AT_SYMLINK_NOFOLLOW)!=0||!same(before,after)){return error(ErrorCode::Conflict,"Public DHT configuration changed while reading");}
  const auto raw=source->document().originalBytes();return Config{std::string(raw),before,std::move(source->document())};
}
std::expected<std::int64_t,Error> integer(const core::Document& d,const char* key,std::int64_t fallback){const auto* node=d.find(key);if(!node){return fallback;}if(node->type!=core::Node::Type::Integer){return error(ErrorCode::InvalidFormat,"Invalid public DHT policy integer");}return node->integer;}
std::expected<std::string,Error> text(const core::Document& d,const char* key){const auto* node=d.find(key);if(!node){return std::string{};}if(node->type!=core::Node::Type::Bytes){return error(ErrorCode::InvalidFormat,"Invalid public DHT bind policy");}return std::string(node->bytes);}
std::expected<void,Error> permitted(const core::Document& d,const AzureusPublicBind& bind){
  for(const char* key:{"Plugin.DHT.dht.enabled",bind.family==4?"dht.net.main_v4.enable":"dht.net.main_v6.enable"}){auto value=integer(d,key,1);if(!value){return std::unexpected(value.error());}if(static_cast<std::uint32_t>(*value)==0){return error(ErrorCode::Unsupported,"Original public DHT network is disabled");}}
  if(bind.family==16){auto value=integer(d,"IPV6 Enable Support",0);if(!value){return std::unexpected(value.error());}if(static_cast<std::uint32_t>(*value)==0){return error(ErrorCode::Unsupported,"Original IPv6 support is disabled");}}
  for(const char* key:{"Enable.Proxy","Enable.SOCKS","Proxy.Data.Enable"}){auto value=integer(d,key,0);if(!value){return std::unexpected(value.error());}if(static_cast<std::uint32_t>(*value)!=0){return error(ErrorCode::Unsupported,"PublicBound requires direct policy; no proxy fallback");}}
  auto filter=integer(d,"Ip Filter Enabled",1);if(!filter){return std::unexpected(filter.error());}if(static_cast<std::uint32_t>(*filter)!=0){return error(ErrorCode::Unsupported,"Native public IP filter adapter is not available");}
  auto port=integer(d,"UDP.NonData.Listen.Port",6881);if(!port){return std::unexpected(port.error());}const auto selected=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*port));if(selected<=0||selected>65535||selected==6880||selected!=bind.port){return error(ErrorCode::Unsupported,"PublicBound must match saved non-data UDP port");}
  auto advanced=integer(d,"Plugin.DHT.dht.advanced",0);if(!advanced){return std::unexpected(advanced.error());}
  if(static_cast<std::uint32_t>(*advanced)!=0){auto override=text(d,"Plugin.DHT.dht.override.ip");if(!override){return std::unexpected(override.error());}if(std::ranges::any_of(*override,[](unsigned char c){return c>0x20;})){return error(ErrorCode::Unsupported,"Saved DHT external override requires its own identity policy");}}
  auto saved=text(d,"Bind IP"),extra=text(d,"Additional Service Bind IPs");auto enforce=integer(d,"Enforce Bind IP",0);if(!saved){return std::unexpected(saved.error());}if(!extra){return std::unexpected(extra.error());}if(!enforce){return std::unexpected(enforce.error());}if(!extra->empty()){return error(ErrorCode::Unsupported,"Additional public binds are not supported");}
  if(saved->empty()){if(static_cast<std::uint32_t>(*enforce)!=0){return error(ErrorCode::Unsupported,"Explicit saved enforced bind required");}return {};}
  std::array<std::uint8_t,16> bytes{};if(saved->find('\0')!=std::string::npos||::inet_pton(bind.family==4?AF_INET:AF_INET6,saved->c_str(),bytes.data())!=1){return error(ErrorCode::Unsupported,"Only same-family numeric saved bind is supported");}
  const auto end=bytes.begin()+bind.family;const bool wildcard=std::all_of(bytes.begin(),end,[](auto b){return b==0;});if(wildcard){if(static_cast<std::uint32_t>(*enforce)!=0){return error(ErrorCode::Unsupported,"Enforced wildcard public bind is unsupported");}return {};}
  if(!std::equal(bytes.begin(),end,bind.address.begin())){return error(ErrorCode::Unsupported,"PublicBound differs from saved explicit bind");}return {};
}
struct Interface {unsigned index=0;std::string name;bool operator==(const Interface&)const=default;};
std::expected<Interface,Error> interface(const AzureusPublicBind& bind){
  ifaddrs* list=nullptr;if(::getifaddrs(&list)!=0){return error(ErrorCode::Io,"Cannot inspect public bound interface");}const auto release=std::unique_ptr<ifaddrs,decltype(&::freeifaddrs)>(list,::freeifaddrs);std::optional<Interface> out;
  for(const auto* at=list;at;at=at->ifa_next){if(!at->ifa_addr||!at->ifa_name||!(at->ifa_flags&IFF_UP)){continue;}const auto* a=at->ifa_addr;const void* bytes=nullptr;
    if(bind.family==4&&a->sa_family==AF_INET){bytes=&reinterpret_cast<const sockaddr_in*>(a)->sin_addr;}
    else if(bind.family==16&&a->sa_family==AF_INET6){const auto* v6=reinterpret_cast<const sockaddr_in6*>(a);if(v6->sin6_scope_id){continue;}bytes=&v6->sin6_addr;}
    if(!bytes||std::memcmp(bytes,bind.address.data(),bind.family)){continue;}Interface next{::if_nametoindex(at->ifa_name),at->ifa_name};if(!next.index){return error(ErrorCode::Io,"Cannot identify public bound interface");}if(out&&*out!=next){return error(ErrorCode::Unsupported,"Ambiguous public bound interface");}out=std::move(next);
  }if(!out){return error(ErrorCode::Unsupported,"Public numeric address is not on a live local interface");}return std::move(*out);
}
}
struct AzureusPublicPolicy::Impl {
  std::shared_ptr<const ProfileLease> lease;std::shared_ptr<AzureusValueVersions> versions;AzureusPublicBind bind;std::string bytes;struct stat stamp{};Interface local;mutable std::atomic<bool> rejected{false};
  std::expected<void,Error> validate()const{
    if(rejected.load()){return error(ErrorCode::Conflict,"Public DHT source was withdrawn; recreate explicitly");}auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}auto held=lease->retain();if(!held){return std::unexpected(held.error());}auto valid=versions->validate();if(!valid){return valid;}
    auto current=config(*lease);if(!current){return std::unexpected(current.error());}if(current->bytes!=bytes||!same(current->stamp,stamp)){return error(ErrorCode::Conflict,"Saved public DHT source changed");}valid=permitted(current->document,bind);if(!valid){return valid;}auto actual=interface(bind);if(!actual){return std::unexpected(actual.error());}if(*actual!=local){return error(ErrorCode::Conflict,"Public DHT bound interface changed");}return {};
  }
};
AzureusPublicPolicy::AzureusPublicPolicy(std::unique_ptr<Impl> value):impl_(std::move(value)){}
AzureusPublicPolicy::~AzureusPublicPolicy()=default;
const AzureusPublicBind& AzureusPublicPolicy::binding()const noexcept{return impl_->bind;}
bool AzureusPublicPolicy::matchesVersions(const AzureusValueVersions& value)const noexcept{return impl_->versions.get()==&value;}
std::shared_ptr<const ProfileLease> AzureusPublicPolicy::profileOwnership()const noexcept{return impl_->lease;}
bool AzureusPublicPolicy::current()const noexcept{return !impl_->rejected.load();}
void AzureusPublicPolicy::revoke()noexcept{impl_->rejected.store(true);}
std::expected<void,Error> AzureusPublicPolicy::validate()const{try{auto valid=impl_->validate();if(!valid){impl_->rejected.store(true);}return valid;}catch(...){impl_->rejected.store(true);return error(ErrorCode::Io,"Cannot validate public DHT source");}}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::open(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,AzureusPublicBind bind){
  try{if(!versions||!versions->profileOwnership()->sharesOwnership(lease)||versions->network()!=bind.network||!bind.port||!((bind.network==0&&bind.family==4)||(bind.network==3&&bind.family==16))||!core::azureusPublicAddress(std::span(bind.address).first(bind.family))){return error(ErrorCode::Unsupported,"Explicit supported public numeric bind and SAME version owner required");}
    auto gate=lease.sourceAccess();if(!gate){return std::unexpected(gate.error());}auto held=lease.retain();if(!held){return std::unexpected(held.error());}auto valid=versions->validate();if(!valid){return std::unexpected(valid.error());}auto source=config(lease);if(!source){return std::unexpected(source.error());}valid=permitted(source->document,bind);if(!valid){return std::unexpected(valid.error());}auto local=interface(bind);if(!local){return std::unexpected(local.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->versions=std::move(versions);out->bind=bind;out->bytes=std::move(source->bytes);out->stamp=source->stamp;out->local=std::move(*local);return std::shared_ptr<AzureusPublicPolicy>(new AzureusPublicPolicy(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot initialize public DHT source");}
}
}
