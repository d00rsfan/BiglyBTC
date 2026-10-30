// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_source.hpp"
#include "sam_protocol.hpp"
#include "file_io.hpp"
#include "plugin_settings.hpp"
#include "torrent_state.hpp"
#include <libtorrent/torrent_info.hpp>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace biglybtc::services::i2p::provider {
namespace {
using storage::Error;using storage::ErrorCode;using storage::detail::Descriptor;
struct Rejected {Error error;};
void require(bool ok,ErrorCode code,const char* reason){if(!ok){throw Rejected{Error{code,reason}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Rejected{value.error()};}return std::move(*value);}
bool unique(const core::Node& n){if(n.type!=core::Node::Type::Dictionary||n.children.size()%2){return false;}std::set<std::string_view> keys;for(std::size_t i=0;i<n.children.size();i+=2){if(n.children[i].type!=core::Node::Type::Bytes||!keys.insert(n.children[i].bytes).second){return false;}}return true;}
bool sameDirectory(const struct stat& a,const struct stat& b){return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_uid==::geteuid()&&b.st_uid==a.st_uid&&a.st_gid==b.st_gid&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode;}
bool sameFile(const struct stat& a,const struct stat& b){return S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&a.st_uid==::geteuid()&&a.st_uid==b.st_uid&&a.st_nlink==1&&b.st_nlink==1&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
bool absent(int directory,const std::string& name){struct stat st{};if(::fstatat(directory,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0){return false;}return errno==ENOENT;}
struct Stamp {std::string config,inventory,state;bool operator==(const Stamp&)const=default;};
Stamp capture(const storage::ProfileLease& lease,const core::TorrentHash& hash,const std::string& directory){
  constexpr core::Limits limits{.inputBytes=8*1024*1024,.stringBytes=8*1024*1024,.nodes=100000,.depth=64};
  require(absent(lease.directoryHandle(),"biglybt.config.saving")&&absent(lease.directoryHandle(),"downloads.config.saving"),ErrorCode::Conflict,"Resolve owned I2P source recovery first");
  auto config=take(storage::ConfigFile::load(lease,"biglybt.config",limits));auto inventory=take(storage::ConfigFile::load(lease,"downloads.config",limits));
  require(config.recoverySource()==storage::RecoverySource::Primary&&inventory.recoverySource()==storage::RecoverySource::Primary&&unique(config.document().root())&&unique(inventory.document().root()),ErrorCode::Conflict,"Owned I2P requires unique primary sources");
  auto binding=storage::originalPluginBinding("azneti2phelper",directory);require(bool(binding),ErrorCode::InvalidFormat,"Invalid original I2P plugin directory");
  storage::OriginalPluginSettings settings(config.document(),std::move(*binding));
  const bool enabled=settings.loadedAtStartup()&&settings.boolean("enable",true)&&settings.boolean("azi2phelper.pure.enabled",true);
  require(enabled&&settings.wrongTypes().empty(),ErrorCode::Unsupported,"Original pure I2P identity is disabled or malformed");
  auto library=core::readLibrary(inventory.document());require(bool(library)&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid owned I2P inventory");
  const auto* rows=inventory.document().find("downloads");if(rows){require(rows->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Invalid I2P inventory rows");for(const auto& row:rows->children){require(unique(row),ErrorCode::InvalidFormat,"Duplicate I2P inventory row");}}
  else{for(std::size_t i=0;i<inventory.document().root().children.size();i+=2){const auto& row=inventory.document().root().children[i+1];if(row.type==core::Node::Type::Dictionary){require(unique(row),ErrorCode::InvalidFormat,"Duplicate legacy I2P inventory row");}}}
  bool found=false;std::set<std::string> seen;
  for(const auto& row:library->downloads){require(row.hash&&seen.insert(row.hash->hex()).second,ErrorCode::Conflict,"Ambiguous owned I2P inventory identity");if(*row.hash==hash){require(row.persistent&&row.startup!=core::StartupState::Stopped,ErrorCode::Unsupported,"Owned I2P download has no current start intent");found=true;}}
  require(found&&std::none_of(library->paused.begin(),library->paused.end(),[&](const auto& p){return p.hash==hash;}),ErrorCode::Conflict,"Owned I2P download is absent or paused");
  Descriptor active(::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));require(active.get()>=0&&absent(active.get(),hash.hex()+".dat.saving"),ErrorCode::Conflict,"Resolve owned I2P metadata saving recovery first");
  auto store=take(storage::TorrentStateStore::open(lease));auto state=take(store.loadAttributes(lease,hash));
  require(!state.recoveredBackup()&&unique(state.document().root()),ErrorCode::Conflict,"Resolve owned I2P metadata recovery first");
  const auto* attrs=state.document().find("attributes");require(attrs&&unique(*attrs),ErrorCode::InvalidFormat,"Unique owned I2P attributes required");
  const auto* networks=attrs->find("networks");require(networks&&networks->type==core::Node::Type::List&&networks->children.size()==1&&networks->children[0].type==core::Node::Type::Bytes&&networks->children[0].bytes=="I2P",ErrorCode::Unsupported,"Dedicated owned admission requires only saved I2P network");
  if(const auto* flags=attrs->find("flags")){require(flags->type==core::Node::Type::Integer&&(static_cast<std::uint64_t>(flags->integer)&0x200U)==0,ErrorCode::Unsupported,"Full owned I2P metadata required");}
  const auto* sources=attrs->find("peersources");require(sources&&sources->type==core::Node::Type::List,ErrorCode::Unsupported,"Explicit original Tracker source required");
  bool tracker=false;for(const auto& item:sources->children){require(item.type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed I2P peer source");tracker|=item.bytes=="Tracker";}
  if(const auto* denied=attrs->find("peersourcesdenied")){require(denied->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Malformed denied I2P peer sources");for(const auto& item:denied->children){require(item.type==core::Node::Type::Bytes&&item.bytes!="Tracker",ErrorCode::Unsupported,"Original I2P Tracker source denied");}}
  require(tracker,ErrorCode::Unsupported,"Original I2P Tracker source disabled");
  return {config.document().serialize(),inventory.document().serialize(),state.document().serialize()};
}
}
struct ProfileSource::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  core::TorrentHash hash;std::string directory;engine::OwnedI2pPolicy policy;
  Descriptor plugins{-1},plugin{-1},key{-1},active{-1};
  struct Bound {int directory=-1;std::string name;Descriptor fd{-1};struct stat stat{};};
  std::vector<Bound> bound;struct stat profileStat{},pluginsStat{},pluginStat{},keyStat{},activeStat{};
  std::optional<ClientIdentity> identity;Destination publicIdentity;std::optional<Stamp> stamp;
  std::vector<std::pair<Hash,std::uint16_t>> trackers;std::shared_ptr<i2cp::EffectAuthority> authority;std::atomic<bool> revoked{false};
  Impl(std::shared_ptr<const storage::ProfileLease> l,core::TorrentHash h,std::string d,engine::OwnedI2pPolicy p):lease(std::move(l)),hash(std::move(h)),directory(std::move(d)),policy(std::move(p)){}
  bool namedCurrent()const{
    struct stat profile{},a{},b{},c{},d{},e{},f{};
    const bool files=std::all_of(bound.begin(),bound.end(),[](const auto& file){struct stat held{},named{};return ::fstat(file.fd.get(),&held)==0&&sameFile(file.stat,held)&&::fstatat(file.directory,file.name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameFile(held,named);});
    struct stat activeHeld{},activeNamed{};const bool activeCurrent=active.get()<0||(::fstat(active.get(),&activeHeld)==0&&sameDirectory(activeStat,activeHeld)&&::fstatat(lease->directoryHandle(),"active",&activeNamed,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(activeHeld,activeNamed));
    return files&&activeCurrent&&lease->valid()&&::fstat(lease->directoryHandle(),&profile)==0&&sameDirectory(profileStat,profile)
      &&::fstat(plugins.get(),&a)==0&&sameDirectory(pluginsStat,a)&&::fstatat(lease->directoryHandle(),"plugins",&b,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(a,b)
      &&::fstat(plugin.get(),&c)==0&&sameDirectory(pluginStat,c)&&::fstatat(plugins.get(),directory.c_str(),&d,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(c,d)
      &&::fstat(key.get(),&e)==0&&sameFile(keyStat,e)&&::fstatat(plugin.get(),"dest_key1.dat",&f,AT_SYMLINK_NOFOLLOW)==0&&sameFile(e,f);
  }
};
ProfileSource::ProfileSource(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
ProfileSource::~ProfileSource()=default;
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::open(const storage::ProfileLease& caller,const core::TorrentHash& hash,std::string directory){
  try{
    auto retained=take(caller.retain());const auto& held=*retained;auto access=take(held.sourceAccess());
    auto binding=storage::originalPluginBinding("azneti2phelper",directory);require(bool(binding),ErrorCode::UnsafePath,"Invalid I2P plugin basename");
    auto impl=std::make_unique<Impl>(retained,hash,std::move(directory),engine::OwnedI2pPolicy(retained,hash));
    require(::fstat(held.directoryHandle(),&impl->profileStat)==0&&S_ISDIR(impl->profileStat.st_mode)&&impl->profileStat.st_uid==::geteuid(),ErrorCode::UnsafePath,"Owned profile directory required");
    impl->plugins=Descriptor(::openat(held.directoryHandle(),"plugins",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    require(impl->plugins.get()>=0&&::fstat(impl->plugins.get(),&impl->pluginsStat)==0,ErrorCode::UnsafePath,"Existing held plugin directories required");
    impl->plugin=Descriptor(::openat(impl->plugins.get(),impl->directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    require(impl->plugin.get()>=0&&::fstat(impl->plugin.get(),&impl->pluginStat)==0,ErrorCode::UnsafePath,"Existing original I2P plugin directory required");
    impl->key=Descriptor(::openat(impl->plugin.get(),"dest_key1.dat",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    require(impl->key.get()>=0&&::fstat(impl->key.get(),&impl->keyStat)==0&&S_ISREG(impl->keyStat.st_mode)&&impl->keyStat.st_uid==::geteuid()&&impl->keyStat.st_nlink==1&&(impl->keyStat.st_mode&0077)==0&&impl->keyStat.st_size>0&&impl->keyStat.st_size<=static_cast<off_t>(MaximumClientIdentityInputBytes),ErrorCode::UnsafePath,"Private original pure identity key required");
    std::array<Byte,MaximumClientIdentityInputBytes> bytes{};struct Wipe{decltype(bytes)& value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{bytes};
    const auto size=static_cast<std::size_t>(impl->keyStat.st_size);std::size_t done=0;
    while(done<size){const auto n=::pread(impl->key.get(),bytes.data()+done,size-done,static_cast<off_t>(done));if(n<0&&errno==EINTR){continue;}require(n>0,ErrorCode::Io,"Cannot read original I2P identity");done+=static_cast<std::size_t>(n);}
    auto identity=decodeClientIdentity(View(bytes).first(size));require(bool(identity),ErrorCode::Unsupported,"Unsupported or invalid original I2P identity");
    impl->publicIdentity=*identity->destination();impl->identity.emplace(std::move(*identity));
    static_cast<void>(capture(held,hash,impl->directory));require(impl->namedCurrent(),ErrorCode::Conflict,"I2P identity source changed during loading");
    return std::shared_ptr<ProfileSource>(new ProfileSource(std::move(impl)));
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot retain owned I2P profile source"});}
}
const std::shared_ptr<const storage::ProfileLease>& ProfileSource::lease()const noexcept{return impl_->lease;}
std::vector<int> ProfileSource::watchDescriptors()const{std::vector<int> result{impl_->lease->directoryHandle(),impl_->plugins.get(),impl_->plugin.get(),impl_->key.get(),impl_->active.get()};for(const auto& file:impl_->bound){result.push_back(file.fd.get());}return result;}
const Destination& ProfileSource::destination()const noexcept{return impl_->publicIdentity;}
const engine::OwnedI2pPolicy& ProfileSource::policy()const noexcept{return impl_->policy;}
const core::TorrentHash& ProfileSource::hash()const noexcept{return impl_->hash;}
bool ProfileSource::prepared()const noexcept{return impl_->stamp.has_value()&&!impl_->revoked;}
void ProfileSource::revoke()noexcept{impl_->revoked=true;if(impl_->authority){impl_->authority->revoke();}}
std::shared_ptr<i2cp::EffectAuthority> ProfileSource::effectAuthority()const noexcept{return impl_->authority;}
bool ProfileSource::revisionCurrent(std::uint64_t revision)const noexcept{return !impl_->revoked&&impl_->lease->sourceRevision()==revision;}
bool ProfileSource::trackerAllowed(const Hash& hash,std::uint16_t port)const noexcept{return std::find(impl_->trackers.begin(),impl_->trackers.end(),std::pair{hash,port})!=impl_->trackers.end();}
std::expected<ClientIdentity,storage::Error> ProfileSource::takeIdentity(){
  auto valid=validate();if(!valid){return std::unexpected(valid.error());}
  if(!impl_->identity){return std::unexpected(Error{ErrorCode::Conflict,"Original I2P identity already consumed"});}
  ClientIdentity result=std::move(*impl_->identity);impl_->identity.reset();return result;
}
std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorage(){
  try{auto access=take(impl_->lease->sourceAccess());require(!impl_->stamp&&!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"Owned I2P preparation is one-shot/current");
    static_cast<void>(capture(*impl_->lease,impl_->hash,impl_->directory));
    auto storage=take(engine::detail::ManagedStorage::prepareOwnedI2p(*impl_->lease,impl_->hash,impl_->policy));
    std::vector<std::string> urls=storage.parameters().trackers;for(const auto& entry:storage.parameters().ti->trackers()){if(std::find(urls.begin(),urls.end(),entry.url)==urls.end()){urls.push_back(entry.url);}}
    require(!urls.empty()&&urls.size()<=4,ErrorCode::Unsupported,"One to four exact I2P HTTP trackers required");
    for(const auto& url:urls){std::string_view authority(url);authority.remove_prefix(7);authority=authority.substr(0,authority.find_first_of("/?#"));auto colon=authority.find(':');auto hash=hashName(authority.substr(0,colon));auto port=colon==std::string_view::npos?std::optional<std::uint64_t>(80):decimal(authority.substr(colon+1),65535);require(hash&&port&&*port,ErrorCode::Unsupported,"Exact numeric I2P tracker port/hash required");impl_->trackers.emplace_back(*hash,static_cast<std::uint16_t>(*port));}
    impl_->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
    require(impl_->active.get()>=0&&::fstat(impl_->active.get(),&impl_->activeStat)==0,ErrorCode::UnsafePath,"Held original active metadata directory required");
    const auto bind=[&](int directory,std::string name){Impl::Bound file;file.directory=directory;file.name=std::move(name);file.fd=Descriptor(::openat(directory,file.name.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));require(file.fd.get()>=0&&::fstat(file.fd.get(),&file.stat)==0&&sameFile(file.stat,file.stat),ErrorCode::UnsafePath,"Private regular owned I2P source file required");impl_->bound.push_back(std::move(file));};
    bind(impl_->lease->directoryHandle(),".azlock");bind(impl_->lease->directoryHandle(),"biglybt.config");bind(impl_->lease->directoryHandle(),"downloads.config");bind(impl_->active.get(),impl_->hash.hex()+".dat");
    impl_->stamp=capture(*impl_->lease,impl_->hash,impl_->directory);require(impl_->namedCurrent(),ErrorCode::Conflict,"Owned I2P source changed during storage preparation");
    const std::weak_ptr<ProfileSource> source=shared_from_this();impl_->authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([source](std::uint64_t revision){auto held=source.lock();return held&&held->revisionCurrent(revision);},impl_->lease->sourceRevision()));return storage;
  }catch(const Rejected& error){impl_->revoked=true;return std::unexpected(error.error);}catch(...){impl_->revoked=true;return std::unexpected(Error{ErrorCode::Io,"Owned I2P storage preparation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validate()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());require(!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P identity source is no longer current");
    const auto now=capture(*impl_->lease,impl_->hash,impl_->directory);require(!impl_->stamp||now==*impl_->stamp,ErrorCode::Conflict,"Owned I2P profile source changed");require(impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P source changed while reading");return {};
  }catch(const Rejected& error){impl_->revoked=true;return std::unexpected(error.error);}catch(...){impl_->revoked=true;return std::unexpected(Error{ErrorCode::Io,"Owned I2P source validation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validateRetiredSnapshot()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    require(impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source no longer names its exact prepared snapshot");
    const auto now=capture(*impl_->lease,impl_->hash,impl_->directory);
    require(now==*impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source changed before checkpoint");
    // Intentionally do not inspect/clear revoked: this proves original bytes
    // for the joined owner's CAS only, and cannot authorize a network effect.
    return {};
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate retired I2P source snapshot"});}
}

}
