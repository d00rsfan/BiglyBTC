// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTPlugin / DHTTransportUDPImpl, cb8c6cb9; bounded explicit-bind slice.
#include "azureus_public_policy.hpp"
#include "file_operation_io.hpp"
#include "state_file_io.hpp"
#include "../platform/ip_socket.hpp"
#include "../platform/network_interfaces.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <atomic>
#include <algorithm>
#include <bit>
namespace biglybtc::storage {
namespace {
std::unexpected<Error> error(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
using detail::StateStamp;
using detail::sameStateSnapshot;
struct Config { std::string bytes; StateStamp stamp; core::Document document; };

std::expected<StateStamp, Error> primaryStamp(const ProfileLease& lease, bool shared) {
  const auto fd = lease.directoryHandle();
  const auto pending = detail::stateStampAt(fd, "biglybt.config.saving");
  if (!pending || *pending) { return error(ErrorCode::Conflict, "Resolve public DHT pending configuration first"); }
  const auto stamp = detail::stateStampAt(fd, "biglybt.config");
  if (!stamp || !*stamp) { return error(ErrorCode::Missing, "Current primary public DHT configuration required"); }
  if (!(*stamp)->regular || !(*stamp)->owned || (*stamp)->links != 1) {
    return error(ErrorCode::UnsafePath, "Unsafe public DHT configuration");
  }
#ifdef _WIN32
  (void)shared;
  const auto witness = detail::openStateWitness(fd, "biglybt.config");
  if (!witness || !fileop::nonOwnerWritesBlocked(witness->get())) {
    return error(ErrorCode::UnsafePath, "Unsafe public DHT configuration");
  }
  const auto held = detail::stateStamp(witness->get());
  if (!held || !sameStateSnapshot(*held, **stamp)) {
    return error(ErrorCode::Conflict, "Public DHT configuration changed while inspecting");
  }
#else
  if ((*stamp)->mode & (shared ? 0002 : 0022)) { return error(ErrorCode::UnsafePath, "Unsafe public DHT configuration"); }
#endif
  return **stamp;
}

std::expected<Config, Error> config(const ProfileLease& lease, bool shared = false) {
  const auto before = primaryStamp(lease, shared);
  if (!before) { return std::unexpected(before.error()); }
  auto source = ConfigFile::load(lease, "biglybt.config");
  if (!source) { return std::unexpected(source.error()); }
  if (source->recoverySource() != RecoverySource::Primary) {
    return error(ErrorCode::Conflict, "Primary public DHT configuration required");
  }
  const auto after = primaryStamp(lease, shared);
  if (!after || !sameStateSnapshot(*before, *after)) {
    return error(ErrorCode::Conflict, "Public DHT configuration changed while reading");
  }
  const auto raw = source->document().originalBytes();
  return Config{std::string(raw), *before, std::move(source->document())};
}
std::expected<std::int64_t,Error> integer(const core::Document& d,const char* key,std::int64_t fallback){const auto* node=d.find(key);if(!node){return fallback;}if(node->type!=core::Node::Type::Integer){return error(ErrorCode::InvalidFormat,"Invalid public DHT policy integer");}return node->integer;}
std::expected<std::string,Error> text(const core::Document& d,const char* key){const auto* node=d.find(key);if(!node){return std::string{};}if(node->type!=core::Node::Type::Bytes){return error(ErrorCode::InvalidFormat,"Invalid public DHT bind policy");}return std::string(node->bytes);}
std::expected<void,Error> permitted(const core::Document& d,const AzureusPublicBind& bind){
  for(const char* key:{"Plugin.DHT.dht.enabled",bind.network==4?"dht.net.biglybt_main_v4.enable":bind.family==4?"dht.net.main_v4.enable":"dht.net.main_v6.enable"}){auto value=integer(d,key,1);if(!value){return std::unexpected(value.error());}if(static_cast<std::uint32_t>(*value)==0){return error(ErrorCode::Unsupported,"Original public DHT network is disabled");}}
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
  const auto interfaces = platform::readInterfaceAddresses(4096);
  if (!interfaces) { return error(ErrorCode::Io, "Cannot inspect public bound interface"); }
  std::optional<Interface> result;
  for (const auto& address : *interfaces) {
    if (!address.up || address.length != bind.family || address.scope ||
        !std::equal(bind.address.begin(), bind.address.begin() + bind.family, address.address.begin())) {
      continue;
    }
    Interface candidate{address.index, address.name};
    if (result && *result != candidate) { return error(ErrorCode::Unsupported, "Ambiguous public bound interface"); }
    result = std::move(candidate);
  }
  if (!result) { return error(ErrorCode::Unsupported, "Public numeric address is not on a live local interface"); }
  return std::move(*result);
}
}
namespace {
core::SharedUdpEndpoint endpoint(const AzureusPublicBind& value){return {value.address,value.port,value.family};}
std::expected<std::string,Error> relevant(const core::Document& document){
 auto out=core::Document::parse("de");if(!out){return error(ErrorCode::Io,"Cannot retain messaging policy");}
 for(const auto* key:{"Plugin.DHT.dht.enabled","dht.net.main_v4.enable","dht.net.main_v6.enable","dht.net.biglybt_main_v4.enable","IPV6 Enable Support","Enable.Proxy","Enable.SOCKS","Proxy.Data.Enable","Ip Filter Enabled","UDP.Listen.Port","UDP.NonData.Listen.Port","UDP.NonData.Listen.Port.Same","UDP.NonData.Listen.Port.Enable","Plugin.DHT.dht.advanced","Plugin.DHT.dht.override.ip","Bind IP","Additional Service Bind IPs","Enforce Bind IP"}){
  unsigned count=0;for(std::size_t i=0;i<document.root().children.size();i+=2){count+=document.root().children[i].bytes==key;}if(count>1){return error(ErrorCode::InvalidFormat,"Ambiguous messaging network policy");}
  if(const auto* node=document.find(key)){auto encoded=core::encodeValue(*node);if(!encoded||!out->replaceValue(key,std::move(*encoded))){return error(ErrorCode::InvalidFormat,"Cannot retain messaging policy");}}
 }return out->serialize();
}
std::expected<void,Error> sharedPermitted(const core::Document& document,const AzureusPublicBind& advertised,const core::SharedUdpEndpoint& local){
 auto samePort=integer(document,"UDP.NonData.Listen.Port.Same",1),enabled=integer(document,"UDP.NonData.Listen.Port.Enable",1);if(!samePort){return std::unexpected(samePort.error());}if(!enabled){return std::unexpected(enabled.error());}if(static_cast<std::uint32_t>(*enabled)==0){return error(ErrorCode::Unsupported,"Original non-data UDP listener is disabled");}
 auto selected=integer(document,static_cast<std::uint32_t>(*samePort)?"UDP.Listen.Port":"UDP.NonData.Listen.Port",6881);if(!selected){return std::unexpected(selected.error());}
 // Existing permitted() keeps proxy/filter/network/bind restrictions. Resolve
 // the original same-port option in this validation copy, never in the file.
 auto copy=core::Document::parse(document.serialize(),compatibleConfigLimits());if(!copy){return error(ErrorCode::InvalidFormat,"Cannot validate shared messaging policy");}
 if(!copy->replaceValue("UDP.NonData.Listen.Port","i"+std::to_string(*selected)+"e")){return error(ErrorCode::InvalidFormat,"Cannot validate saved messaging port");}
 AzureusPublicBind physical=advertised;physical.address=local.address;physical.port=local.port;
 return permitted(*copy,physical);
}
}
struct AzureusPublicPolicy::Impl {
  std::shared_ptr<const ProfileLease> lease;std::shared_ptr<AzureusValueVersions> versions;AzureusPublicBind bind;std::string bytes;StateStamp stamp{};Interface local;
  std::shared_ptr<const core::SharedUdpWitness> witness;core::SharedUdpEndpoint socket;
  std::string fingerprint;mutable std::shared_ptr<core::Document> cached;mutable StateStamp cachedStamp{};
  std::expected<void,Error> refreshShared()const{
    if(!witness->current(socket,endpoint(bind))){return error(ErrorCode::Conflict,"Shared messaging listener was withdrawn");}
    const auto now = primaryStamp(*lease, true);
    if (!now) { return std::unexpected(now.error()); }
    if (cached && sameStateSnapshot(*now, cachedStamp)) { return {}; }
    auto value=config(*lease,true);if(!value){return std::unexpected(value.error());}auto selected=relevant(value->document);if(!selected){return std::unexpected(selected.error());}
    if(*selected!=fingerprint){return error(ErrorCode::Conflict,"Saved messaging network policy changed; recreate the service");}
    auto allowed=sharedPermitted(value->document,bind,socket);if(!allowed){return allowed;}
    cached=std::make_shared<core::Document>(std::move(value->document));cachedStamp=value->stamp;return {};
  }
  mutable std::atomic<bool> rejected{false};
  std::expected<void,Error> validate()const{
    if(rejected.load()){return error(ErrorCode::Conflict,"Public DHT source was withdrawn; recreate explicitly");}auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}auto held=lease->retain();if(!held){return std::unexpected(held.error());}auto valid=versions->validate();if(!valid){return valid;}
    if(witness){return refreshShared();}
    auto current=config(*lease);if(!current){return std::unexpected(current.error());}if(current->bytes!=bytes||!sameStateSnapshot(current->stamp,stamp)){return error(ErrorCode::Conflict,"Saved public DHT source changed");}valid=permitted(current->document,bind);if(!valid){return valid;}auto actual=interface(bind);if(!actual){return std::unexpected(actual.error());}if(*actual!=local){return error(ErrorCode::Conflict,"Public DHT bound interface changed");}return {};
  }
};
AzureusPublicPolicy::AzureusPublicPolicy(std::unique_ptr<Impl> value):impl_(std::move(value)){}
AzureusPublicPolicy::~AzureusPublicPolicy()=default;
const AzureusPublicBind& AzureusPublicPolicy::binding()const noexcept{return impl_->bind;}
bool AzureusPublicPolicy::matchesVersions(const AzureusValueVersions& value)const noexcept{return impl_->versions.get()==&value;}
std::shared_ptr<const ProfileLease> AzureusPublicPolicy::profileOwnership()const noexcept{return impl_->lease;}
bool AzureusPublicPolicy::current()const noexcept{return !impl_->rejected.load()&&(!impl_->witness||impl_->witness->current(impl_->socket,endpoint(impl_->bind)));}
void AzureusPublicPolicy::revoke()noexcept{impl_->rejected.store(true);}
std::expected<void,Error> AzureusPublicPolicy::validate()const{try{auto valid=impl_->validate();if(!valid){impl_->rejected.store(true);}return valid;}catch(...){impl_->rejected.store(true);return error(ErrorCode::Io,"Cannot validate public DHT source");}}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::open(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,AzureusPublicBind bind){
  try{if(!versions||!versions->profileOwnership()->sharesOwnership(lease)||versions->network()!=bind.network||!bind.port||!((bind.network==0&&bind.family==4)||(bind.network==3&&bind.family==16))||!core::azureusPublicAddress(std::span(bind.address).first(bind.family))){return error(ErrorCode::Unsupported,"Explicit supported public numeric bind and SAME version owner required");}
    auto gate=lease.sourceAccess();if(!gate){return std::unexpected(gate.error());}auto held=lease.retain();if(!held){return std::unexpected(held.error());}auto valid=versions->validate();if(!valid){return std::unexpected(valid.error());}auto source=config(lease);if(!source){return std::unexpected(source.error());}valid=permitted(source->document,bind);if(!valid){return std::unexpected(valid.error());}auto local=interface(bind);if(!local){return std::unexpected(local.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->versions=std::move(versions);out->bind=bind;out->bytes=std::move(source->bytes);out->stamp=source->stamp;out->local=std::move(*local);return std::shared_ptr<AzureusPublicPolicy>(new AzureusPublicPolicy(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot initialize public DHT source");}
}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::openShared(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,AzureusPublicBind advertised,core::SharedUdpEndpoint local,std::shared_ptr<const core::SharedUdpWitness> witness){return openSharedImpl(lease,std::move(versions),advertised,local,std::move(witness),false);}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::openBootstrapShared(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,std::int32_t network,core::SharedUdpEndpoint local,std::shared_ptr<const core::SharedUdpWitness> witness){
 AzureusPublicBind placeholder;placeholder.network=network;placeholder.family=local.length;placeholder.address=local.address;placeholder.port=local.port;
 return openSharedImpl(lease,std::move(versions),placeholder,local,std::move(witness),false,true);
}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::openOwnedShared(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,AzureusPublicBind bind,std::shared_ptr<const core::SharedUdpWitness> witness){return openSharedImpl(lease,std::move(versions),bind,endpoint(bind),std::move(witness),true);}
std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> AzureusPublicPolicy::openSharedImpl(const ProfileLease& lease,std::shared_ptr<AzureusValueVersions> versions,AzureusPublicBind advertised,core::SharedUdpEndpoint local,std::shared_ptr<const core::SharedUdpWitness> witness,bool owned,bool bootstrap){
 try{
  const bool family=advertised.family==4||advertised.family==16;
  const bool loopback=family&&(advertised.family==4?advertised.address[0]==127:(std::all_of(advertised.address.begin(),advertised.address.end()-1,[](auto b){return b==0;})&&advertised.address.back()==1));
  const bool scope=owned?((advertised.network==4||(advertised.network==0&&advertised.family==4)||(advertised.network==3&&advertised.family==16))&&loopback&&local==endpoint(advertised)):(family&&(((advertised.network==0||advertised.network==4)&&advertised.family==4)||(advertised.network==3&&advertised.family==16))&&(bootstrap?local==endpoint(advertised):core::azureusPublicAddress(std::span(advertised.address).first(advertised.family))));
  if(!scope||!versions||!versions->profileOwnership()->sharesOwnership(lease)||versions->network()!=advertised.network||!witness||!witness->current(local,endpoint(advertised))||local.length!=advertised.family){return error(ErrorCode::Unsupported,"Current qualified shared listener and public address required");}
  auto gate=lease.sourceAccess();if(!gate){return std::unexpected(gate.error());}auto held=lease.retain();if(!held){return std::unexpected(held.error());}auto value=config(lease,true);if(!value){return std::unexpected(value.error());}
  auto allowed=sharedPermitted(value->document,advertised,local);if(!allowed){return std::unexpected(allowed.error());}auto fingerprint=relevant(value->document);if(!fingerprint){return std::unexpected(fingerprint.error());}
  auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->versions=std::move(versions);out->bind=advertised;out->socket=local;out->witness=std::move(witness);out->fingerprint=std::move(*fingerprint);out->cachedStamp=value->stamp;out->cached=std::make_shared<core::Document>(std::move(value->document));auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}
  return std::shared_ptr<AzureusPublicPolicy>(new AzureusPublicPolicy(std::move(out)));
 }catch(...){return error(ErrorCode::Io,"Cannot initialize shared messaging policy");}
}
std::expected<void,Error> AzureusPublicPolicy::validateChatIdentity(std::string_view key,const ChatIdentityStamp& expected)const{
 try{auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}auto valid=validate();if(!valid){return valid;}if(!impl_->witness||!impl_->cached){return error(ErrorCode::Unsupported,"Shared messaging source scope required");}
  auto stamp=chatIdentityStamp(*impl_->cached,key);if(!stamp){return std::unexpected(stamp.error());}if(*stamp!=expected){return error(ErrorCode::Conflict,"Saved messaging identity changed");}return {};
 }catch(...){return error(ErrorCode::Io,"Cannot validate messaging identity");}
}

}
