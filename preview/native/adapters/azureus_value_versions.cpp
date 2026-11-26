// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_value_versions.hpp"
#include "azureus_profile_files.hpp"
#include <array>
#include <mutex>
#include <set>
namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;
using detail::StateHandle;
using Capture = detail::ProfileFileSnapshot;
namespace files = detail::azureusFiles;
constexpr core::Limits Limits{.inputBytes=65536,.stringBytes=65536,.nodes=2048,.depth=32};
std::unexpected<Error> error(ErrorCode c,const char* text){return std::unexpected(Error{c,text});}
std::expected<Capture, Error> capture(StateHandle directory, const std::string& name) {
  return files::capture(directory, name, Limits.inputBytes);
}
std::expected<core::Document,Error> parse(std::string_view bytes){
  auto doc=core::Document::parse(std::string(bytes),Limits);if(!doc||doc->root().type!=core::Node::Type::Dictionary){return error(ErrorCode::InvalidFormat,"Invalid Azureus version map");}
  std::set<std::string_view> keys;for(std::size_t i=0;i<doc->root().children.size();i+=2){if(!keys.insert(doc->root().children[i].bytes).second){return error(ErrorCode::InvalidFormat,"Duplicate Azureus version key");}}
  if(const auto* next=doc->find("next");next&&next->type!=core::Node::Type::Integer){return error(ErrorCode::InvalidFormat,"Invalid Azureus next version type");}return std::move(*doc);
}
}
struct AzureusValueVersions::Impl {
  std::shared_ptr<const ProfileLease> lease;StateDescriptor dht,net;std::int32_t network=0;std::string netName;
  Capture primary;std::uint32_t next=0;unsigned remaining=0;bool poisoned=false;mutable std::mutex mutex;
  StateHandle dir()const{return network?net.get():dht.get();}
  std::expected<void,Error> validate()const {
    if(poisoned){return error(ErrorCode::Conflict,"Azureus version reservation requires explicit recovery");}
    auto current=lease->retain();if(!current){return std::unexpected(current.error());}
    if(!files::currentDirectory(lease->directoryHandle(),"dht",dht.get())||(network&&!files::currentDirectory(dht.get(),netName,net.get()))){return error(ErrorCode::Conflict,"Azureus version directory changed");}
    auto p=capture(dir(),"version.dat"),pending=capture(dir(),"version.saving");if(!p){return std::unexpected(p.error());}if(!pending){return std::unexpected(pending.error());}
    if(!primary.matches(*p)||pending->bytes){return error(ErrorCode::Conflict,"Azureus version source or pending publication changed");}return {};
  }
};
AzureusValueVersions::AzureusValueVersions(std::unique_ptr<Impl> value):impl_(std::move(value)){}
AzureusValueVersions::~AzureusValueVersions()=default;
std::int32_t AzureusValueVersions::network()const noexcept{return impl_->network;}
std::shared_ptr<const ProfileLease> AzureusValueVersions::profileOwnership()const noexcept{return impl_->lease;}
std::expected<std::shared_ptr<AzureusValueVersions>,Error> AzureusValueVersions::open(const ProfileLease& source,std::int32_t network){
  try{
    if(network<0){return error(ErrorCode::InvalidFormat,"Invalid Azureus network");}auto access=source.sourceAccess(true);if(!access){return std::unexpected(access.error());}auto held=source.retain();if(!held){return std::unexpected(held.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->network=network;auto dht=files::openDirectory(out->lease->directoryHandle(),"dht",true);if(!dht){return std::unexpected(dht.error());}out->dht=std::move(*dht);
    if(network){out->netName="net"+std::to_string(network);auto net=files::openDirectory(out->dht.get(),out->netName,true);if(!net){return std::unexpected(net.error());}out->net=std::move(*net);}
    auto primary=capture(out->dir(),"version.dat"),pending=capture(out->dir(),"version.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    if(primary->bytes){auto doc=parse(*primary->bytes);if(!doc){return std::unexpected(doc.error());}}
    if(pending->bytes){
      // Original reads .saving ONLY when .dat is absent. Recover precisely that
      // complete source; never delete an uncertain companion of a live primary.
      if(primary->bytes){return error(ErrorCode::Conflict,"Review Azureus version pending publication");}auto doc=parse(*pending->bytes);if(!doc){return std::unexpected(doc.error());}
      auto live=out->lease->retain();if(!live){return std::unexpected(live.error());}
      if(!files::currentDirectory(out->lease->directoryHandle(),"dht",out->dht.get())||(network&&!files::currentDirectory(out->dht.get(),out->netName,out->net.get()))){return error(ErrorCode::Conflict,"Azureus version parent changed before recovery");}
      const auto latest = capture(out->dir(), "version.saving");
      if (!latest || !pending->matches(*latest)) {
        return error(ErrorCode::Conflict, "Azureus pending version changed before recovery");
      }
      primary = files::publish(out->dir(), "version.saving", "version.dat", *latest, false, Limits.inputBytes);
      if (!primary) { return std::unexpected(primary.error()); }

    }
    out->primary=std::move(*primary);auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}return std::shared_ptr<AzureusValueVersions>(new AzureusValueVersions(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot allocate Azureus version owner");}
}
std::expected<std::shared_ptr<const ProfileLease>,Error> AzureusValueVersions::retainProfile()const{
  try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard guard(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}return impl_->lease->retain();}
  catch(...){return error(ErrorCode::Io,"Cannot retain Azureus value profile");}
}
std::expected<void,Error> AzureusValueVersions::validate()const {try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard guard(impl_->mutex);return impl_->validate();}catch(...){return error(ErrorCode::Io,"Cannot validate Azureus version owner");}}
std::expected<std::int32_t,Error> AzureusValueVersions::next(std::int64_t now){
  try{
    auto access=impl_->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}
    std::lock_guard guard(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}
    if(!impl_->remaining){
      auto doc=parse(impl_->primary.bytes.value_or("de"));if(!doc){return std::unexpected(doc.error());}const auto* saved=doc->find("next");const auto reserved=originalAzureusVersionReservation(saved?std::optional{saved->integer}:std::nullopt,now);
      if(!doc->replaceValue("next","i"+std::to_string(reserved.next)+'e')){return error(ErrorCode::InvalidFormat,"Cannot encode Azureus version reservation");}const auto bytes=doc->serialize();
      impl_->poisoned = true;
      auto staged = files::stage(impl_->dir(), "version.saving", bytes, Limits.inputBytes);
      if (!staged) { return std::unexpected(staged.error()); }
      auto held = impl_->lease->retain();
      if (!held) { return std::unexpected(held.error()); }
      const auto primary = capture(impl_->dir(), "version.dat");
      if (!primary || !impl_->primary.matches(*primary)
          || !files::currentDirectory(impl_->lease->directoryHandle(), "dht", impl_->dht.get())
          || (impl_->network && !files::currentDirectory(impl_->dht.get(), impl_->netName, impl_->net.get()))) {
        return error(ErrorCode::Conflict, "Azureus version publication preimage changed");
      }
      auto published = files::publish(impl_->dir(), "version.saving", "version.dat", *staged,
          impl_->primary.bytes.has_value(), Limits.inputBytes);
      if (!published) { return std::unexpected(published.error()); }
      impl_->primary=std::move(*published);impl_->next=static_cast<std::uint32_t>(reserved.first);impl_->remaining=128;impl_->poisoned=false;
    }
    --impl_->remaining;const auto answer=std::bit_cast<std::int32_t>(impl_->next);++impl_->next;return answer;
  }catch(...){impl_->poisoned=true;return error(ErrorCode::Io,"Azureus version reservation failed");}
}
}
