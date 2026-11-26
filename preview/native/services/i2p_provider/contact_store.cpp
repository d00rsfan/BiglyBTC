// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact_store.hpp"
#include "state_file_io.hpp"
#include <mutex>

namespace biglybtc::storage {
namespace {
namespace dht=services::i2p::dht;
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
std::unexpected<Error> error(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}

struct Input {
  StateDescriptor fd;
  std::optional<StateStamp> stamp;
  std::optional<std::string> bytes;
};

// Keep a metadata witness for the key. Only the separate contact cache is read.
std::expected<Input,Error> capture(StateHandle dir, const std::string& name, bool contents) {
  const auto label = detail::stateStampAt(dir, name);
  if (!label) { return std::unexpected(label.error()); }
  if (!*label) { return Input{}; }
  const auto& expected = **label;
  if (!expected.regular || !expected.owned || expected.links != 1 || expected.size < 0
      || (contents && static_cast<std::uintmax_t>(expected.size) > dht::MaximumSavedContactsBytes)) {
    return error(ErrorCode::UnsafePath, "Unsafe I2P contact input inode");
  }
  auto fd = detail::openStateWitness(dir, name);
  if (!fd) { return std::unexpected(fd.error()); }
  const auto before = detail::stateStamp(fd->get());
  if (!before || !detail::sameStateSnapshot(*before, expected)) {
    return error(ErrorCode::Conflict, "I2P contact input changed while opening");
  }
  Input result{std::move(*fd), expected, std::nullopt};
  if (contents) {
    auto stream = detail::openStateWitness(dir, name, true);
    if (!stream) { return std::unexpected(stream.error()); }
    const auto streamBefore = detail::stateStamp(stream->get());
    if (!streamBefore || !detail::sameStateSnapshot(*streamBefore, expected)) {
      return error(ErrorCode::Conflict, "I2P contact stream changed while opening");
    }
    result.bytes.emplace(static_cast<std::size_t>(expected.size), '\0');
    std::size_t offset = 0;
    while (offset < result.bytes->size()) {
      const auto count = detail::readStateAt(stream->get(), result.bytes->data() + offset,
          result.bytes->size() - offset, offset);
      if (count <= 0) { return error(ErrorCode::Conflict, "I2P contact input changed while reading"); }
      offset += static_cast<std::size_t>(count);
    }
    char extra{};
    if (detail::readStateAt(stream->get(), &extra, 1, offset) != 0) {
      return error(ErrorCode::Conflict, "I2P contact input grew while reading");
    }
    const auto after = detail::stateStamp(stream->get());
    if (!after || !detail::sameStateSnapshot(*after, expected)) {
      return error(ErrorCode::Conflict, "I2P contact stream changed while reading");
    }
  }
  const auto held = detail::stateStamp(result.fd.get());
  const auto named = detail::stateStampAt(dir, name);
  if (!held || !named || !*named || !detail::sameStateSnapshot(*held, expected)
      || !detail::sameStateSnapshot(**named, expected)) {
    return error(ErrorCode::Conflict, "I2P contact input name changed while reading");
  }
  return result;
}

bool equal(const Input& a,const Input& b){
  return a.bytes == b.bytes && a.stamp.has_value() == b.stamp.has_value()
      && (!a.stamp || detail::sameStateSnapshot(*a.stamp, *b.stamp));
}
bool heldUnchanged(const Input& input) {
  if (!input.stamp) { return true; }
  const auto actual = detail::stateStamp(input.fd.get());
  return actual && detail::sameStateSnapshot(*actual, *input.stamp);
}

struct Directory { StateDescriptor fd; StateStamp stamp{}; };

bool namedDirectory(StateHandle parent, const std::string& name, const Directory& directory) {
  const auto held = detail::stateStamp(directory.fd.get());
  const auto named = detail::stateStampAt(parent, name);
  return held && named && *named && detail::sameStateDirectory(*held, directory.stamp)
      && detail::sameStateDirectory(**named, directory.stamp);
}

std::expected<Directory,Error> directory(StateHandle parent, const std::string& name, bool create = false) {
  const auto named = detail::stateStampAt(parent, name);
  if (!named) { return std::unexpected(named.error()); }
  auto fd = create && !*named ? detail::createStateDirectory(parent, name) : detail::openStateDirectory(parent, name);
  if (!fd) { return std::unexpected(fd.error()); }
  const auto stamp = detail::stateStamp(fd->get());
  if (!stamp || !stamp->directory || !stamp->owned) {
    return error(ErrorCode::UnsafePath, "Owned original I2P contact directory required");
  }
  Directory result{std::move(*fd), *stamp};
  if (!namedDirectory(parent, name, result)) {
    return error(ErrorCode::Conflict, "Original I2P contact directory identity changed");
  }
  return result;
}

bool profileNamed(const std::filesystem::path& path, const StateStamp& expected) {
  auto directory = detail::openStatePathDirectory(path);
  if (!directory) { return false; }
  const auto stamp = detail::stateStamp(directory->get());
  return stamp && stamp->owned && detail::sameStateDirectory(*stamp, expected);
}
std::expected<std::vector<dht::SavedContact>,Error> decode(const std::string& bytes){
  auto value=dht::decodeSavedContacts(services::i2p::View(reinterpret_cast<const services::i2p::Byte*>(bytes.data()),bytes.size()));
  if(!value){return error(ErrorCode::InvalidFormat,"Invalid or unsupported original I2P contact cache");}return std::move(*value);
}
bool cancelled(std::stop_token stop,std::chrono::steady_clock::time_point end){return stop.stop_requested()||std::chrono::steady_clock::now()>=end;}
}

struct I2pContacts::Impl {
  std::shared_ptr<const ProfileLease> lease;
  std::filesystem::path profile;
  std::string cacheName,keyName;
  StateStamp profileStamp{};
  Directory plugins,helper,cache;
  Input key,primary,pending;
  unsigned index=0;bool secondary=false,canWrite=false,poisoned=false;
  std::vector<dht::SavedContact> contacts;
  mutable std::mutex mutex;
  std::expected<void,Error> roots()const{
    auto retained=lease->retain();if(!retained){return std::unexpected(retained.error());}
    const auto actual=detail::stateStamp(lease->directoryHandle());
    if(!actual||!detail::sameStateDirectory(*actual,profileStamp)||!profileNamed(profile,profileStamp)||!namedDirectory(lease->directoryHandle(),"plugins",plugins)||!namedDirectory(plugins.fd.get(),"azneti2phelper",helper)||!namedDirectory(helper.fd.get(),cacheName,cache)){
      return error(ErrorCode::Conflict,"Original I2P contact directory identity changed");
    }
    auto current=capture(helper.fd.get(),keyName,false);if(!current){return std::unexpected(current.error());}
    if(!heldUnchanged(key)||!equal(*current,key)){return error(ErrorCode::Conflict,"Original I2P key input changed");}return {};
  }
  std::expected<void,Error> validate()const{
    if(poisoned){return error(ErrorCode::Conflict,"I2P contact cache requires explicit recovery");}
    auto valid=roots();if(!valid){return valid;}
    auto p=capture(cache.fd.get(),"contacts.dat",true),s=capture(cache.fd.get(),"contacts.saving",true);
    if(!p){return std::unexpected(p.error());}if(!s){return std::unexpected(s.error());}
    if(!heldUnchanged(primary)||!heldUnchanged(pending)||!equal(*p,primary)||!equal(*s,pending)){
      return error(ErrorCode::Conflict,"Original I2P contact file changed");
    }return {};
  }
};
I2pContacts::I2pContacts(std::unique_ptr<Impl> value):impl_(std::move(value)){}
I2pContacts::~I2pContacts()=default;
unsigned I2pContacts::index()const noexcept{return impl_->index;}
bool I2pContacts::secondary()const noexcept{return impl_->secondary;}
std::shared_ptr<const ProfileLease> I2pContacts::profileOwnership()const noexcept{return impl_->lease;}
std::expected<std::shared_ptr<I2pContacts>,Error> I2pContacts::open(const ProfileLease& lease,unsigned index,bool secondary){
  try{
    if(index>1){return error(ErrorCode::InvalidFormat,"Original I2P contact index must be zero or one");}
    auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
    auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*retained);out->index=index;out->secondary=secondary;
    auto path=detail::stateDirectoryPath(out->lease->directoryHandle());if(!path){return std::unexpected(path.error());}out->profile=std::move(*path);
    const auto root=detail::stateStamp(out->lease->directoryHandle());
    if(!root||!profileNamed(out->profile,*root)){return error(ErrorCode::UnsafePath,"Original I2P profile pathname changed");}
    out->profileStamp=*root;
    auto plugins=directory(out->lease->directoryHandle(),"plugins");if(!plugins){return std::unexpected(plugins.error());}out->plugins=std::move(*plugins);
    auto helper=directory(out->plugins.fd.get(),"azneti2phelper");if(!helper){return std::unexpected(helper.error());}out->helper=std::move(*helper);
    const std::string suffix=(index?"1":"")+std::string(secondary?"_s":"");out->cacheName="dhtdata"+suffix;out->keyName="dest_key"+suffix+".dat";
    auto key=capture(out->helper.fd.get(),out->keyName,false);if(!key){return std::unexpected(key.error());}out->key=std::move(*key);
    auto cache=directory(out->helper.fd.get(),out->cacheName,true);if(!cache){return std::unexpected(cache.error());}out->cache=std::move(*cache);
    auto primary=capture(out->cache.fd.get(),"contacts.dat",true),pending=capture(out->cache.fd.get(),"contacts.saving",true);
    if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    const auto& selected=primary->bytes?primary->bytes:pending->bytes;
    if(selected){auto contacts=decode(*selected);if(!contacts){return std::unexpected(contacts.error());}out->contacts=std::move(*contacts);}
    out->canWrite=!(primary->stamp&&pending->stamp);out->primary=std::move(*primary);out->pending=std::move(*pending);
    auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}
    if(!out->primary.stamp&&out->pending.stamp){
      const auto original=*out->pending.stamp;
      if(!detail::publishStateFile(out->cache.fd.get(),"contacts.saving","contacts.dat",detail::InvalidStateHandle,false,&original)||!detail::syncStateDirectory(out->cache.fd.get())){return error(ErrorCode::Io,"Cannot durably recover original I2P contact cache");}
      auto p=capture(out->cache.fd.get(),"contacts.dat",true),s=capture(out->cache.fd.get(),"contacts.saving",true);
      if(!p||!s||!p->stamp||s->stamp||!detail::sameStateIdentity(*p->stamp,original)||p->bytes!=out->pending.bytes){return error(ErrorCode::Conflict,"Recovered I2P contact inode changed");}
      out->primary=std::move(*p);out->pending=std::move(*s);valid=out->validate();if(!valid){return std::unexpected(valid.error());}
    }
    return std::shared_ptr<I2pContacts>(new I2pContacts(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot open original I2P contact cache");}
}
std::expected<void,Error> I2pContacts::validate()const{
  try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);return impl_->validate();}
  catch(...){return error(ErrorCode::Io,"Cannot validate original I2P contact cache");}
}
std::expected<std::shared_ptr<const I2pContactsSnapshot>,Error> I2pContacts::snapshot()const{
  try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}return std::make_shared<const I2pContactsSnapshot>(I2pContactsSnapshot{impl_->contacts,impl_->canWrite});}
  catch(...){return error(ErrorCode::Io,"Cannot copy original I2P contact cache");}
}
std::expected<void,Error> I2pContacts::save(std::span<const dht::SavedContact> values,std::stop_token stop,std::chrono::steady_clock::time_point end){
  try{
    auto access=impl_->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);
    auto valid=impl_->validate();if(!valid){return valid;}if(!impl_->canWrite){return error(ErrorCode::Unsupported,"Ambiguous original I2P contact files remain unchanged");}
    if(values.size()>dht::MaximumSavedContacts){return error(ErrorCode::Unsupported,"Original I2P contact export maximum32");}
    // Copy and validate the owning snapshot before any filesystem effect.
    std::vector<dht::SavedContact> accepted(values.begin(),values.end());
    auto encoded=dht::encodeSavedContacts(accepted);if(!encoded){return error(ErrorCode::InvalidFormat,"Invalid original I2P contact export");}
    const std::string bytes(reinterpret_cast<const char*>(encoded->data()),encoded->size());
    if(cancelled(stop,end)){return error(ErrorCode::Cancelled,"I2P contact export cancelled or expired");}
    impl_->poisoned=true;auto created=detail::stageStateFile(impl_->cache.fd.get(),"contacts.saving",bytes,0600);if(!created){return std::unexpected(created.error());}
    auto synced=detail::syncStateFile(created->get());if(!synced){return synced;}
    valid=impl_->roots();if(!valid){return valid;}
    auto primary=capture(impl_->cache.fd.get(),"contacts.dat",true),pending=capture(impl_->cache.fd.get(),"contacts.saving",true);
    const auto actual=detail::stateStamp(created->get());
    if(!primary||!pending||!heldUnchanged(impl_->primary)||!equal(*primary,impl_->primary)||!pending->stamp||pending->bytes!=std::optional{bytes}||!actual||!detail::sameStateSnapshot(*actual,*pending->stamp)){
      return error(ErrorCode::Conflict,"I2P contact publication preimage changed");
    }
    if(cancelled(stop,end)){return error(ErrorCode::Cancelled,"I2P contact export cancelled or expired before publication");}
    // Cooperative writers share the OFD gate. These checks do not claim atomic
    // exclusion against an uncooperative pathname swap between check/rename.
#ifdef _WIN32
    impl_->primary.fd = {};
    primary->fd = {};
#endif
    const auto previous = impl_->primary.stamp ? &*impl_->primary.stamp : nullptr;
    const auto publishedState = detail::publishStateFile(impl_->cache.fd.get(), "contacts.saving", "contacts.dat",
        created->get(), previous != nullptr, &*actual, previous);
    if (!publishedState) { return std::unexpected(publishedState.error()); }
    if (!detail::syncStateDirectory(impl_->cache.fd.get())) { return error(ErrorCode::Io,"Cannot durably publish original I2P contacts"); }
    auto published=capture(impl_->cache.fd.get(),"contacts.dat",true),absent=capture(impl_->cache.fd.get(),"contacts.saving",true);
    if(!published||!absent||!published->stamp||absent->stamp||published->bytes!=std::optional{bytes}||!detail::sameStateIdentity(*published->stamp,*actual)){return error(ErrorCode::Conflict,"Published I2P contact inode changed");}
    valid=impl_->roots();if(!valid){return valid;}
    impl_->primary=std::move(*published);impl_->pending=std::move(*absent);impl_->contacts=std::move(accepted);impl_->poisoned=false;return {};
  }catch(...){std::lock_guard lock(impl_->mutex);impl_->poisoned=true;return error(ErrorCode::Io,"Original I2P contact publication failed");}
}
} // namespace biglybtc::storage
