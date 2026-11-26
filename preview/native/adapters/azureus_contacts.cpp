// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_contacts.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include "azureus_profile_files.hpp"
#include <bit>
#include <mutex>
namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;
using detail::StateHandle;
using Capture = detail::ProfileFileSnapshot;
namespace files = detail::azureusFiles;
constexpr std::size_t MaximumInput=MaximumAzureusContactFile;
std::unexpected<Error> error(ErrorCode c,const char* text){return std::unexpected(Error{c,text});}
std::expected<Capture, Error> capture(StateHandle directory, const std::string& name) {
  return files::capture(directory, name, MaximumInput);
}
}

std::expected<std::vector<AzureusSavedContact>,Error> decodeAzureusContactState(std::string_view wire){
  if(wire.size()<4||wire.size()>MaximumAzureusContactFile){return error(ErrorCode::InvalidFormat,"Bounded original contact state required");}
  std::size_t at=0;const auto get=[&](unsigned count){std::uint64_t value=0;while(count--){value=(value<<8)|static_cast<unsigned char>(wire[at++]);}return value;};
  const auto count=get(4);if(count>MaximumAzureusSavedContacts){return error(ErrorCode::Unsupported,"Original contact state exceeds native32 envelope");}
  std::vector<AzureusSavedContact> out;out.reserve(count);
  for(std::uint64_t i=0;i<count;++i){
    if(wire.size()-at<11){return error(ErrorCode::InvalidFormat,"Truncated original contact record");}
    AzureusSavedContact value;value.timeAlive=std::bit_cast<std::int64_t>(get(8));const auto type=get(1);value.protocol=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(get(1)));value.length=static_cast<std::uint8_t>(get(1));
    if(type!=1||(value.length!=4&&value.length!=16)||wire.size()-at<value.length+std::size_t{2}){return error(ErrorCode::InvalidFormat,"Invalid original UDP contact record");}
    for(std::size_t j=0;j<value.length;++j){value.address[j]=static_cast<std::uint8_t>(get(1));}value.port=static_cast<std::uint16_t>(get(2));out.push_back(value);
  }
  if(at!=wire.size()){return error(ErrorCode::InvalidFormat,"Trailing original contact state bytes");}return out;
}
std::expected<std::string,Error> encodeAzureusContactState(std::span<const AzureusSavedContact> values){
  if(values.size()>MaximumAzureusSavedContacts){return error(ErrorCode::Unsupported,"Original contact export maximum32");}
  std::string out;out.reserve(MaximumAzureusContactFile);const auto put=[&](std::uint64_t value,unsigned count){for(unsigned i=count;i>0;--i){out.push_back(static_cast<char>(value>>((i-1)*8)));}};put(values.size(),4);
  for(const auto& value:values){if(value.length!=4&&value.length!=16){return error(ErrorCode::InvalidFormat,"Invalid original contact family");}put(static_cast<std::uint64_t>(value.timeAlive),8);put(1,1);put(static_cast<std::uint8_t>(value.protocol),1);put(value.length,1);for(std::size_t j=0;j<value.length;++j){put(value.address[j],1);}put(value.port,2);}return out;
}
namespace {
bool writable(std::span<const AzureusSavedContact> contacts,std::uint8_t family,AzureusContactScope scope){
  for(const auto& value:contacts){
    if(value.length!=family||!value.port||value.protocol<51||value.protocol>55){return false;}
    if(scope==AzureusContactScope::PublicBound){if(!core::azureusPublicAddress(std::span(value.address).first(family))){return false;}continue;}
    if(family==4){if(value.address[0]!=127){return false;}}
    else{for(std::size_t i=0;i<15;++i){if(value.address[i]){return false;}}if(value.address[15]!=1){return false;}}
  }return true;
}
}
struct AzureusContacts::Impl {
  std::shared_ptr<const ProfileLease> lease;StateDescriptor dht,net;std::int32_t network=0;std::uint8_t family=4;AzureusContactScope scope=AzureusContactScope::OwnedLoopback;std::string netName;
  Capture primary,pending;std::vector<AzureusSavedContact> contacts;bool canWrite=false,poisoned=false;mutable std::mutex mutex;
  StateHandle dir()const{return network?net.get():dht.get();}
  std::expected<void,Error> validate()const{
    if(poisoned){return error(ErrorCode::Conflict,"Azureus contacts require explicit recovery");}auto current=lease->retain();if(!current){return std::unexpected(current.error());}
    if(!files::currentDirectory(lease->directoryHandle(),"dht",dht.get())||(network&&!files::currentDirectory(dht.get(),netName,net.get()))){return error(ErrorCode::Conflict,"Azureus contact directory changed");}
    auto p=capture(dir(),"contacts.dat"),s=capture(dir(),"contacts.saving");if(!p){return std::unexpected(p.error());}if(!s){return std::unexpected(s.error());}
    if(!primary.matches(*p)||!pending.matches(*s)){return error(ErrorCode::Conflict,"Azureus contact source changed");}return {};
  }
};
AzureusContacts::AzureusContacts(std::unique_ptr<Impl> p):impl_(std::move(p)){}
AzureusContacts::~AzureusContacts()=default;
std::int32_t AzureusContacts::network()const noexcept{return impl_->network;}
std::uint8_t AzureusContacts::family()const noexcept{return impl_->family;}
AzureusContactScope AzureusContacts::scope()const noexcept{return impl_->scope;}
std::shared_ptr<const ProfileLease> AzureusContacts::profileOwnership()const noexcept{return impl_->lease;}
std::expected<std::shared_ptr<AzureusContacts>,Error> AzureusContacts::open(const ProfileLease& source,std::int32_t network,std::uint8_t family,AzureusContactScope scope){
  try{
    if((scope!=AzureusContactScope::OwnedLoopback&&scope!=AzureusContactScope::PublicBound)||(scope==AzureusContactScope::PublicBound&&!(((network==0||network==4)&&family==4)||(network==3&&family==16)))||network<0||(family!=4&&family!=16)){return error(ErrorCode::InvalidFormat,"Invalid contact profile network/family");}auto access=source.sourceAccess(true);if(!access){return std::unexpected(access.error());}auto held=source.retain();if(!held){return std::unexpected(held.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->network=network;out->family=family;out->scope=scope;auto dht=files::openDirectory(out->lease->directoryHandle(),"dht",true);if(!dht){return std::unexpected(dht.error());}out->dht=std::move(*dht);
    if(network){out->netName="net"+std::to_string(network);auto net=files::openDirectory(out->dht.get(),out->netName,true);if(!net){return std::unexpected(net.error());}out->net=std::move(*net);}
    auto primary=capture(out->dir(),"contacts.dat"),pending=capture(out->dir(),"contacts.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    const auto selected=primary->bytes?primary->bytes:pending->bytes;
    if(selected){auto decoded=decodeAzureusContactState(*selected);if(!decoded){return std::unexpected(decoded.error());}out->contacts=std::move(*decoded);}
    out->canWrite=writable(out->contacts,family,scope)&&!(primary->bytes&&pending->bytes);
    if(!primary->bytes&&pending->bytes&&out->canWrite){
      auto fresh = out->lease->retain();
      if (!fresh) { return std::unexpected(fresh.error()); }
      const auto latest = capture(out->dir(), "contacts.saving");
      if (!latest || !pending->matches(*latest) ||
          !files::currentDirectory(out->lease->directoryHandle(), "dht", out->dht.get()) ||
          (network && !files::currentDirectory(out->dht.get(), out->netName, out->net.get()))) {
        return error(ErrorCode::Conflict, "Contact pending recovery changed");
      }
      primary = files::publish(out->dir(), "contacts.saving", "contacts.dat", *latest, false, MaximumInput);
      if (!primary) { return std::unexpected(primary.error()); }
      pending = capture(out->dir(), "contacts.saving");
      if (!pending) { return std::unexpected(pending.error()); }

    }
    out->primary=std::move(*primary);out->pending=std::move(*pending);auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}return std::shared_ptr<AzureusContacts>(new AzureusContacts(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot initialize contact cache owner");}
}
std::expected<void,Error> AzureusContacts::validate()const{try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);return impl_->validate();}catch(...){return error(ErrorCode::Io,"Cannot validate contact cache");}}
std::expected<std::shared_ptr<const AzureusContactsSnapshot>,Error> AzureusContacts::snapshot()const{try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}return std::make_shared<const AzureusContactsSnapshot>(AzureusContactsSnapshot{impl_->contacts,impl_->canWrite});}catch(...){return error(ErrorCode::Io,"Cannot copy contact cache");}}
std::expected<void,Error> AzureusContacts::save(std::span<const AzureusSavedContact> values,std::stop_token stop,std::chrono::steady_clock::time_point deadline){
  try{
    auto access=impl_->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}
    if(!impl_->canWrite||!writable(values,impl_->family,impl_->scope)){return error(ErrorCode::Unsupported,"Foreign/ambiguous original cache remains unchanged");}auto bytes=encodeAzureusContactState(values);if(!bytes){return std::unexpected(bytes.error());}if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){return error(ErrorCode::Cancelled,"Contact export cancelled/expired");}
    impl_->poisoned = true;
    auto staged = files::stage(impl_->dir(), "contacts.saving", *bytes, MaximumInput);
    if (!staged) { return std::unexpected(staged.error()); }
    auto held = impl_->lease->retain();
    if (!held) { return std::unexpected(held.error()); }
    const auto primary = capture(impl_->dir(), "contacts.dat");
    if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
      return error(ErrorCode::Cancelled, "Contact export cancelled/expired before publication");
    }
    if (!primary || !impl_->primary.matches(*primary)
        || !files::currentDirectory(impl_->lease->directoryHandle(), "dht", impl_->dht.get())
        || (impl_->network && !files::currentDirectory(impl_->dht.get(), impl_->netName, impl_->net.get()))) {
      return error(ErrorCode::Conflict, "Contact publication preimage changed");
    }
    auto published = files::publish(impl_->dir(), "contacts.saving", "contacts.dat", *staged,
        impl_->primary.bytes.has_value(), MaximumInput);
    if (!published) { return std::unexpected(published.error()); }
    impl_->primary=std::move(*published);impl_->pending={};impl_->contacts.assign(values.begin(),values.end());impl_->poisoned=false;return {};
  }catch(...){std::lock_guard lock(impl_->mutex);impl_->poisoned=true;return error(ErrorCode::Io,"Contact publication failed");}
}
}
