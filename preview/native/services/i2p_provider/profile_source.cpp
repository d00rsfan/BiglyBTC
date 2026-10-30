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
constexpr core::Limits SourceLimits{.inputBytes=8*1024*1024,.stringBytes=8*1024*1024,.nodes=100000,.depth=64};
struct Stamp {std::string config,inventory,state;bool operator==(const Stamp&)const=default;};
Stamp capture(const storage::ProfileLease& lease,const core::TorrentHash* hash,const std::string& directory,bool running=true){
  require(absent(lease.directoryHandle(),"biglybt.config.saving")&&absent(lease.directoryHandle(),"downloads.config.saving"),ErrorCode::Conflict,"Resolve owned I2P source recovery first");
  auto config=take(storage::ConfigFile::load(lease,"biglybt.config",SourceLimits));auto inventory=take(storage::ConfigFile::load(lease,"downloads.config",SourceLimits));
  require(config.recoverySource()==storage::RecoverySource::Primary&&inventory.recoverySource()==storage::RecoverySource::Primary&&unique(config.document().root())&&unique(inventory.document().root()),ErrorCode::Conflict,"Owned I2P requires unique primary sources");
  auto binding=storage::originalPluginBinding("azneti2phelper",directory);require(bool(binding),ErrorCode::InvalidFormat,"Invalid original I2P plugin directory");
  storage::OriginalPluginSettings settings(config.document(),std::move(*binding));
  const bool enabled=settings.loadedAtStartup()&&settings.boolean("enable",true)&&settings.boolean("azi2phelper.pure.enabled",true);
  require(enabled&&settings.wrongTypes().empty(),ErrorCode::Unsupported,"Original pure I2P identity is disabled or malformed");
  auto library=core::readLibrary(inventory.document());require(bool(library)&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid owned I2P inventory");
  const auto* rows=inventory.document().find("downloads");if(rows){require(rows->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Invalid I2P inventory rows");for(const auto& row:rows->children){require(unique(row),ErrorCode::InvalidFormat,"Duplicate I2P inventory row");}}
  else{for(std::size_t i=0;i<inventory.document().root().children.size();i+=2){const auto& row=inventory.document().root().children[i+1];if(row.type==core::Node::Type::Dictionary){require(unique(row),ErrorCode::InvalidFormat,"Duplicate legacy I2P inventory row");}}}
  bool found=false;std::set<std::string> seen;
  for(const auto& row:library->downloads){require(row.hash&&seen.insert(row.hash->hex()).second,ErrorCode::Conflict,"Ambiguous owned I2P inventory identity");if(hash&&*row.hash==*hash){require(row.persistent&&(!running||row.startup!=core::StartupState::Stopped),ErrorCode::Unsupported,"Owned I2P download has no current start intent");found=true;}}
  if(!hash){
    for(const auto* pending:{"BiglyBTC.torrent-import","BiglyBTC.torrent-import.saving","BiglyBTC.torrent-import.inventory","BiglyBTC.torrent-import.backup"}){
      require(absent(lease.directoryHandle(),pending),ErrorCode::Conflict,"Resolve I2P inventory import recovery first");}
    return {config.document().serialize(),inventory.document().serialize(),{}};
  }
  require(found&&(!running||std::none_of(library->paused.begin(),library->paused.end(),[&](const auto& p){return p.hash==*hash;})),ErrorCode::Conflict,"Owned I2P download is absent or paused");
  Descriptor active(::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));require(active.get()>=0&&absent(active.get(),hash->hex()+".dat.saving"),ErrorCode::Conflict,"Resolve owned I2P metadata saving recovery first");
  auto store=take(storage::TorrentStateStore::open(lease));auto state=take(store.loadAttributes(lease,*hash));
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
std::string raw(const core::Document& d,const core::Node& n){return d.originalBytes().substr(n.begin,n.end-n.begin);}
std::string inventoryScope(const std::string& bytes,const core::TorrentHash& hash,bool selected){
  auto d=take(core::Document::parse(bytes,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Cannot project I2P inventory"};}));
  auto library=take(core::readLibrary(d).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Cannot project I2P rows"};}));
  const core::SavedDownload* target=nullptr;for(const auto& row:library.downloads){if(row.hash&&*row.hash==hash){target=&row;}}
  std::string out="d",pauses="l";
  for(std::size_t i=0;i<d.root().children.size();i+=2){
    const auto& key=d.root().children[i];const auto& value=d.root().children[i+1];
    if(key.bytes=="downloads"){
      std::string rows="l";for(std::size_t j=0;j<value.children.size();++j){if((target&&j==target->sourceIndex)==selected){rows+=raw(d,value.children[j]);}}
      out+=raw(d,key)+rows+'e';
    }else if(key.bytes=="pause_data"){
      require(value.type==core::Node::Type::List,ErrorCode::InvalidFormat,"Invalid I2P pause list");
      for(const auto& pause:value.children){const auto* encoded=pause.find("hash");
        require(encoded&&encoded->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid I2P pause hash");
        auto parsed=core::TorrentHash::parse(encoded->bytes);require(bool(parsed),ErrorCode::InvalidFormat,"Invalid I2P pause identity");
        if((*parsed==hash)==selected){pauses+=raw(d,pause);}}
    }else if(library.legacyMap&&target&&target->legacyKey&&key.bytes==*target->legacyKey){
      if(selected){out+=raw(d,key)+raw(d,value);}
    }else if(!selected){out+=raw(d,key)+raw(d,value);}
  }return out+"10:pause_data"+pauses+"ee";
}
std::string positionInvariant(const std::string& bytes,bool published=false){
  auto d=take(core::Document::parse(bytes,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Cannot project I2P positions"};}));
  auto library=take(core::readLibrary(d).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Cannot project I2P position rows"};}));
  const auto rowWithoutPosition=[&](const core::Node& row){
    require(unique(row),ErrorCode::InvalidFormat,"Unique I2P position row required");std::string out="d";
    for(std::size_t j=0;j<row.children.size();j+=2){const auto& key=row.children[j];const auto& value=row.children[j+1];
      if(key.bytes=="position"){require(value.type==core::Node::Type::Integer&&(!published||value.integer>0),ErrorCode::InvalidFormat,"Invalid published I2P queue position");}
      else{out+=raw(d,key)+raw(d,value);}}
    return out+'e';
  };
  std::string out="d";for(std::size_t i=0;i<d.root().children.size();i+=2){const auto& key=d.root().children[i];const auto& value=d.root().children[i+1];out+=raw(d,key);
    if(key.bytes=="downloads"){
      require(value.type==core::Node::Type::List,ErrorCode::InvalidFormat,"I2P position list required");out+='l';for(const auto& row:value.children){out+=rowWithoutPosition(row);}out+='e';
    }else if(library.legacyMap&&std::any_of(library.downloads.begin(),library.downloads.end(),[&](const auto& row){return row.sourceIndex==i/2;})){out+=rowWithoutPosition(value);}
    else{out+=raw(d,value);}
  }return out+'e';
}
Stamp memberCapture(const storage::ProfileLease& lease,const core::TorrentHash& hash,const std::string& directory,bool running=true){
  auto value=capture(lease,&hash,directory,running);value.inventory=inventoryScope(value.inventory,hash,true);return value;
}
}
struct ProfileSource::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  core::TorrentHash hash;std::string directory;engine::OwnedI2pPolicy policy;
  Descriptor plugins{-1},plugin{-1},key{-1},active{-1};
  struct Bound {int directory=-1;std::string name;Descriptor fd{-1};struct stat stat{};};
  std::vector<Bound> bound;struct stat profileStat{},pluginsStat{},pluginStat{},keyStat{},activeStat{};
  std::optional<ClientIdentity> identity;Destination publicIdentity;std::optional<Stamp> stamp,accepted;
  std::vector<std::pair<Hash,std::uint16_t>> trackers;std::shared_ptr<i2cp::EffectAuthority> authority;std::atomic<bool> revoked{false},dirty{false};
  bool session=false,publishing=false;
  std::shared_ptr<ProfileSource> anchor;
  std::vector<std::weak_ptr<ProfileSource>> children;
  std::vector<engine::detail::ManagedPayloadClaim> claimed;
  std::vector<storage::OriginalTorrentIdentity> retainedIdentities;
  std::vector<engine::detail::ManagedPayloadClaim> admissionClaims;
  std::optional<storage::OriginalTorrentIdentity> admissionIdentity;
  const Stamp* snapshot()const noexcept{return stamp?&*stamp:accepted?&*accepted:nullptr;}
  struct TrackerGrant {Hash hash;std::uint16_t port;std::weak_ptr<ProfileSource> child;};
  std::atomic<std::shared_ptr<const std::vector<TrackerGrant>>> trackerGrants;
  void bind(int directory,std::string name){
    Bound file;file.directory=directory;file.name=std::move(name);file.fd=Descriptor(::openat(directory,file.name.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));
    struct stat named{};require(file.fd.get()>=0&&::fstat(file.fd.get(),&file.stat)==0&&sameFile(file.stat,file.stat)
        &&::fstatat(directory,file.name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameFile(file.stat,named),ErrorCode::UnsafePath,"Private current regular I2P source required");
    auto old=std::find_if(bound.begin(),bound.end(),[&](const auto& v){return v.directory==directory&&v.name==file.name;});
    if(old==bound.end()){bound.push_back(std::move(file));}else{*old=std::move(file);}
  }
  void refreshTrackers(){
    auto grants=std::make_shared<std::vector<TrackerGrant>>();
    for(const auto& weak:children){if(auto child=weak.lock();child&&child->impl_->stamp&&!child->impl_->revoked){
      for(const auto& [hash,port]:child->impl_->trackers){grants->push_back({hash,port,child});}}}
    trackerGrants.store(std::move(grants));
  }
  Impl(std::shared_ptr<const storage::ProfileLease> l,core::TorrentHash h,std::string d,engine::OwnedI2pPolicy p):lease(std::move(l)),hash(std::move(h)),directory(std::move(d)),policy(std::move(p)){}
  bool namedCurrent()const{
    struct stat profile{},a{},b{},c{},d{},e{},f{};
    const bool files=std::all_of(bound.begin(),bound.end(),[](const auto& file){struct stat held{},named{};return ::fstat(file.fd.get(),&held)==0&&sameFile(file.stat,held)&&::fstatat(file.directory,file.name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameFile(held,named);});
    struct stat activeHeld{},activeNamed{};const bool activeCurrent=active.get()<0||(::fstat(active.get(),&activeHeld)==0&&sameDirectory(activeStat,activeHeld)&&::fstatat(lease->directoryHandle(),"active",&activeNamed,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(activeHeld,activeNamed));
    if(anchor){return files&&activeCurrent&&lease->valid();}
    return files&&activeCurrent&&lease->valid()&&::fstat(lease->directoryHandle(),&profile)==0&&sameDirectory(profileStat,profile)
      &&::fstat(plugins.get(),&a)==0&&sameDirectory(pluginsStat,a)&&::fstatat(lease->directoryHandle(),"plugins",&b,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(a,b)
      &&::fstat(plugin.get(),&c)==0&&sameDirectory(pluginStat,c)&&::fstatat(plugins.get(),directory.c_str(),&d,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(c,d)
      &&::fstat(key.get(),&e)==0&&sameFile(keyStat,e)&&::fstatat(plugin.get(),"dest_key1.dat",&f,AT_SYMLINK_NOFOLLOW)==0&&sameFile(e,f);
  }
};
ProfileSource::ProfileSource(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
ProfileSource::~ProfileSource()=default;
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::open(const storage::ProfileLease& caller,const core::TorrentHash& hash,std::string directory){return openImpl(caller,hash,std::move(directory),false);}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::createSession(const storage::ProfileLease& caller,std::string directory){return openImpl(caller,core::TorrentHash{},std::move(directory),true);}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::openImpl(const storage::ProfileLease& caller,const core::TorrentHash& hash,std::string directory,bool session){
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
    impl->session=session;
    auto initial=capture(held,session?nullptr:&hash,impl->directory);
    if(session){
      impl->bind(held.directoryHandle(),".azlock");impl->bind(held.directoryHandle(),"biglybt.config");impl->bind(held.directoryHandle(),"downloads.config");
      impl->active=Descriptor(::openat(held.directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
      require(impl->active.get()>=0&&::fstat(impl->active.get(),&impl->activeStat)==0,ErrorCode::UnsafePath,"Held I2P metadata directory required");impl->stamp=std::move(initial);
    }
    require(impl->namedCurrent(),ErrorCode::Conflict,"I2P identity source changed during loading");
    auto result=std::shared_ptr<ProfileSource>(new ProfileSource(std::move(impl)));
    if(session){const std::weak_ptr<ProfileSource> weak=result;result->impl_->authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([weak](std::uint64_t revision){auto held=weak.lock();return held&&held->revisionCurrent(revision);},held.sourceRevision()));}
    return result;
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot retain owned I2P profile source"});}
}
const std::shared_ptr<const storage::ProfileLease>& ProfileSource::lease()const noexcept{return impl_->lease;}
std::vector<int> ProfileSource::watchDescriptors()const{std::vector<int> result;for(int fd:{impl_->lease->directoryHandle(),impl_->plugins.get(),impl_->plugin.get(),impl_->key.get(),impl_->active.get()}){if(fd>=0){result.push_back(fd);}}for(const auto& file:impl_->bound){result.push_back(file.fd.get());}return result;}
std::vector<int> ProfileSource::criticalWatchDescriptors()const{std::vector<int> result;if(impl_->key.get()>=0){result.push_back(impl_->key.get());}for(const auto& file:impl_->bound){if(file.name==".azlock"||file.name=="biglybt.config"){result.push_back(file.fd.get());}}return result;}
bool ProfileSource::groupMode()const noexcept{return impl_->session||bool(impl_->anchor);}
void ProfileSource::invalidate()noexcept{impl_->dirty=true;if(impl_->anchor){impl_->anchor->impl_->dirty=true;}}
const Destination& ProfileSource::destination()const noexcept{return impl_->publicIdentity;}
const engine::OwnedI2pPolicy& ProfileSource::policy()const noexcept{return impl_->policy;}
const core::TorrentHash& ProfileSource::hash()const noexcept{return impl_->hash;}
bool ProfileSource::prepared()const noexcept{return impl_->stamp.has_value()&&!impl_->revoked;}
void ProfileSource::revoke()noexcept{impl_->revoked=true;if(impl_->authority){impl_->authority->revoke();}}
std::shared_ptr<i2cp::EffectAuthority> ProfileSource::effectAuthority()const noexcept{return impl_->authority;}
bool ProfileSource::revisionCurrent(std::uint64_t revision)const noexcept{return !impl_->revoked&&!impl_->dirty&&(!impl_->anchor||(!impl_->anchor->impl_->revoked&&!impl_->anchor->impl_->dirty))&&impl_->lease->sourceRevision()==revision;}
bool ProfileSource::trackerAllowed(const Hash& hash,std::uint16_t port)const noexcept{
  if(impl_->revoked){return false;}if(impl_->session){auto grants=impl_->trackerGrants.load();if(!grants){return false;}
    return std::any_of(grants->begin(),grants->end(),[&](const auto& grant){auto child=grant.child.lock();return child&&!child->impl_->revoked&&grant.hash==hash&&grant.port==port;});}
  return std::find(impl_->trackers.begin(),impl_->trackers.end(),std::pair{hash,port})!=impl_->trackers.end();
}
std::expected<ClientIdentity,storage::Error> ProfileSource::takeIdentity(){
  auto valid=validate();if(!valid){return std::unexpected(valid.error());}
  if(!impl_->identity){return std::unexpected(Error{ErrorCode::Conflict,"Original I2P identity already consumed"});}
  ClientIdentity result=std::move(*impl_->identity);impl_->identity.reset();return result;
}
std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorage(){
  if(impl_->session){return std::unexpected(Error{ErrorCode::Unsupported,"Session anchor has no payload admission"});}
  if(!impl_->anchor){return prepareStorageDirect();}
  std::optional<engine::detail::ManagedStorage> result;
  auto saved=impl_->anchor->publish(impl_->hash,[&]()->std::expected<void,Error>{auto value=prepareStorageDirect();if(!value){return std::unexpected(value.error());}result.emplace(std::move(*value));return {};});
  if(!saved){revoke();return std::unexpected(saved.error());}
  if(impl_->revoked){return std::unexpected(Error{ErrorCode::Cancelled,"I2P child retired during preparation"});}
  return std::move(*result);
}
std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorageDirect(){
  try{auto access=take(impl_->lease->sourceAccess());require(!impl_->session&&!impl_->stamp&&!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"Owned I2P preparation is one-shot/current");
    if(impl_->anchor){require(impl_->accepted&&memberCapture(*impl_->lease,impl_->hash,impl_->directory)==*impl_->accepted,ErrorCode::Conflict,"I2P child source changed after acceptance");}
    else{static_cast<void>(capture(*impl_->lease,&impl_->hash,impl_->directory));}
    auto storage=take(engine::detail::ManagedStorage::prepareOwnedI2p(*impl_->lease,impl_->hash,impl_->policy,impl_->claimed,impl_->retainedIdentities));
    std::vector<std::string> urls=storage.parameters().trackers;for(const auto& entry:storage.parameters().ti->trackers()){if(std::find(urls.begin(),urls.end(),entry.url)==urls.end()){urls.push_back(entry.url);}}
    require(!urls.empty()&&urls.size()<=4,ErrorCode::Unsupported,"One to four exact I2P HTTP trackers required");
    for(const auto& url:urls){std::string_view authority(url);authority.remove_prefix(7);authority=authority.substr(0,authority.find_first_of("/?#"));auto colon=authority.find(':');auto hash=hashName(authority.substr(0,colon));auto port=colon==std::string_view::npos?std::optional<std::uint64_t>(80):decimal(authority.substr(colon+1),65535);require(hash&&port&&*port,ErrorCode::Unsupported,"Exact numeric I2P tracker port/hash required");impl_->trackers.emplace_back(*hash,static_cast<std::uint16_t>(*port));}
    if(impl_->active.get()<0){
      impl_->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
      require(impl_->active.get()>=0&&::fstat(impl_->active.get(),&impl_->activeStat)==0,ErrorCode::UnsafePath,"Held original active metadata directory required");
    }
    if(!impl_->anchor){impl_->bind(impl_->lease->directoryHandle(),".azlock");impl_->bind(impl_->lease->directoryHandle(),"biglybt.config");impl_->bind(impl_->lease->directoryHandle(),"downloads.config");}
    impl_->bind(impl_->active.get(),impl_->hash.hex()+".dat");
    impl_->stamp=impl_->anchor?memberCapture(*impl_->lease,impl_->hash,impl_->directory):capture(*impl_->lease,&impl_->hash,impl_->directory);
    require(impl_->namedCurrent(),ErrorCode::Conflict,"Owned I2P source changed during storage preparation");
    if(impl_->anchor){impl_->admissionClaims.assign(storage.claims().begin(),storage.claims().end());impl_->admissionIdentity=storage.identity();}
    const std::weak_ptr<ProfileSource> source=shared_from_this();impl_->authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([source](std::uint64_t revision){auto held=source.lock();return held&&held->revisionCurrent(revision);},impl_->lease->sourceRevision()));return storage;
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"Owned I2P storage preparation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validate()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    require(!impl_->revoked,ErrorCode::Conflict,"Owned I2P source is revoked");
    if(impl_->anchor){
      auto valid=impl_->anchor->validate();if(!valid){return std::unexpected(valid.error());}
      require(impl_->namedCurrent(),ErrorCode::Conflict,"I2P child metadata path changed");
      const auto now=memberCapture(*impl_->lease,impl_->hash,impl_->directory);require(!impl_->snapshot()||now==*impl_->snapshot(),ErrorCode::Conflict,"I2P selected source changed");
    }else{
      require(impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P identity source is no longer current");
      const auto now=capture(*impl_->lease,impl_->session?nullptr:&impl_->hash,impl_->directory);
      require(!impl_->stamp||now==*impl_->stamp,ErrorCode::Conflict,"Owned I2P profile source changed");
      if(impl_->session){for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&!child->impl_->revoked&&child->impl_->snapshot()){
        require(child->impl_->namedCurrent()&&memberCapture(*impl_->lease,child->impl_->hash,impl_->directory)==*child->impl_->snapshot(),ErrorCode::Conflict,"Active I2P child source changed");}}}
    }
    require(impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P source changed while reading");impl_->dirty=false;return {};
  }catch(const Rejected& error){const_cast<ProfileSource*>(this)->revoke();return std::unexpected(error.error);}catch(...){const_cast<ProfileSource*>(this)->revoke();return std::unexpected(Error{ErrorCode::Io,"Owned I2P source validation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validateRetiredSnapshot()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    require(impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source no longer names its exact prepared snapshot");
    if(impl_->anchor){auto valid=impl_->anchor->validate();if(!valid){return std::unexpected(valid.error());}}
    const auto now=impl_->anchor?memberCapture(*impl_->lease,impl_->hash,impl_->directory,false):capture(*impl_->lease,&impl_->hash,impl_->directory);
    require(now==*impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source changed before checkpoint");
    return {}; // Read-only; intentionally never restores revoked effects.
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate retired I2P source snapshot"});}
}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::prepareChild(const core::TorrentHash& hash,
    std::span<const engine::detail::ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> identities){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing,ErrorCode::Conflict,"I2P child requires its session anchor");
    auto current=validate();if(!current){return std::unexpected(current.error());}
    require(claimed.size()<=engine::detail::MaximumManagedPayloadClaims&&identities.size()<=engine::detail::MaximumManagedPayloadClaims,ErrorCode::Unsupported,"I2P retained admission bound");
    std::erase_if(impl_->children,[](const auto& weak){return weak.expired();});
    require(impl_->children.size()<32,ErrorCode::Unsupported,"I2P session member bound");
    for(const auto& weak:impl_->children){if(auto child=weak.lock()){
      require(child->hash()!=hash,ErrorCode::Conflict,"I2P child already retained");
      for(const auto& required:child->impl_->admissionClaims){require(std::any_of(claimed.begin(),claimed.end(),[&](const auto& value){return value.absolutePath==required.absolutePath&&value.device==required.device&&value.inode==required.inode;}),ErrorCode::Conflict,"Retained I2P sibling payload claim omitted");}
      if(child->impl_->admissionIdentity){const auto& required=*child->impl_->admissionIdentity;
        require(std::any_of(identities.begin(),identities.end(),[&](const auto& value){return value.originalKey==required.originalKey&&value.selected==required.selected&&value.full==required.full;}),ErrorCode::Conflict,"Retained I2P sibling full identity omitted");}
    }}
    auto accepted=memberCapture(*impl_->lease,hash,impl_->directory);
    auto data=std::make_unique<Impl>(impl_->lease,hash,impl_->directory,engine::OwnedI2pPolicy(impl_->lease,hash));
    data->anchor=shared_from_this();data->publicIdentity=impl_->publicIdentity;data->accepted=std::move(accepted);
    data->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
    require(data->active.get()>=0&&::fstat(data->active.get(),&data->activeStat)==0,ErrorCode::UnsafePath,"Held I2P child metadata directory required");
    data->bind(data->active.get(),hash.hex()+".dat");require(data->namedCurrent(),ErrorCode::Conflict,"I2P child changed while accepting");
    data->claimed.assign(claimed.begin(),claimed.end());data->retainedIdentities.assign(identities.begin(),identities.end());
    auto child=std::shared_ptr<ProfileSource>(new ProfileSource(std::move(data)));impl_->children.push_back(child);return child;
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare owned I2P child"});}
}
std::expected<void,storage::Error> ProfileSource::publish(const core::TorrentHash& target,
    const std::function<std::expected<void,storage::Error>()>& operation){return publishImpl(target,operation);}
std::expected<void,storage::Error> ProfileSource::publishPositions(
    const std::function<std::expected<void,storage::Error>()>& operation){return publishImpl({},operation);}
std::expected<void,storage::Error> ProfileSource::publishImpl(const std::optional<core::TorrentHash>& target,
    const std::function<std::expected<void,storage::Error>()>& operation){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing&&bool(operation),ErrorCode::Conflict,"I2P publication requires one nonreentrant anchor operation");
    auto valid=validate();if(!valid){return std::unexpected(valid.error());}
    const auto others=target?inventoryScope(impl_->stamp->inventory,*target,false):positionInvariant(impl_->stamp->inventory);
    struct BeforeName {int directory;std::string name;struct stat value;};
    struct BeforeMember {std::shared_ptr<ProfileSource> source;Stamp stamp;std::vector<BeforeName> names;};
    std::vector<BeforeMember> members;
    for(const auto& weak:impl_->children){if(auto child=weak.lock()){
      BeforeMember before{child,memberCapture(*impl_->lease,child->hash(),impl_->directory,false),{}};
      for(const auto& bound:child->impl_->bound){before.names.push_back({bound.directory,bound.name,bound.stat});}members.push_back(std::move(before));}}
    impl_->publishing=true;struct Reset {bool& value;~Reset(){value=false;}} reset{impl_->publishing};
    auto result=operation();if(!result){
      // A rejected command may advance the cooperative epoch before any write.
      // Independent before-images prevent a changed target from blessing itself
      // by updating its own stored snapshot in the callback.
      bool unchanged=impl_->namedCurrent()&&capture(*impl_->lease,nullptr,impl_->directory)==*impl_->stamp;
      for(const auto& before:members){
        unchanged=unchanged&&before.source->impl_->namedCurrent()
            &&memberCapture(*impl_->lease,before.source->hash(),impl_->directory,false)==before.stamp;
        for(const auto& file:before.names){struct stat named{};unchanged=unchanged&&::fstatat(file.directory,file.name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameFile(file.value,named);}
      }
      if(!unchanged){revoke();}else{impl_->dirty=false;if(impl_->authority){impl_->authority->revision_=impl_->lease->sourceRevision();}}
      return std::unexpected(result.error());
    }
    require(!impl_->revoked,ErrorCode::Cancelled,"I2P group retired during publication");
    auto after=capture(*impl_->lease,nullptr,impl_->directory);
    require(after.config==impl_->stamp->config&&(target?inventoryScope(after.inventory,*target,false):positionInvariant(after.inventory,true))==others,ErrorCode::Conflict,"I2P publication changed unrelated source rows");
    // Immutable common named files remain exact. Only downloads.config may have
    // been durably replaced by this actual trusted operation.
    auto inventory=std::find_if(impl_->bound.begin(),impl_->bound.end(),[](const auto& file){return file.name=="downloads.config";});
    require(inventory!=impl_->bound.end(),ErrorCode::Conflict,"I2P inventory binding unavailable");
    impl_->bound.erase(inventory);
    require(impl_->namedCurrent(),ErrorCode::Conflict,"I2P shared identity/config changed during publication");
    impl_->bind(impl_->lease->directoryHandle(),"downloads.config");
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&child->impl_->snapshot()){
      auto& member=*child->impl_;
      if(!target){
        auto now=memberCapture(*impl_->lease,member.hash,impl_->directory,false);const auto& before=*member.snapshot();
        require(member.namedCurrent()&&now.config==before.config&&now.state==before.state
            &&positionInvariant(now.inventory,true)==positionInvariant(before.inventory),ErrorCode::Conflict,"I2P position publication changed child state");
        if(member.stamp){member.stamp->inventory=std::move(now.inventory);}else{member.accepted->inventory=std::move(now.inventory);}
      }else if(member.hash==*target){
        auto rows=take(core::Document::parse(after.inventory,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published I2P inventory"};}));
        auto saved=take(core::readLibrary(rows).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published I2P rows"};}));
        const bool started=std::any_of(saved.downloads.begin(),saved.downloads.end(),[&](const auto& row){return row.hash&&*row.hash==*target&&row.persistent&&row.startup!=core::StartupState::Stopped;})
            &&std::none_of(saved.paused.begin(),saved.paused.end(),[&](const auto& pause){return pause.hash==*target;});
        if(!started){child->revoke();}else if(member.stamp){
          member.stamp=memberCapture(*impl_->lease,*target,impl_->directory);
          member.bind(member.active.get(),target->hex()+".dat");require(member.namedCurrent(),ErrorCode::Conflict,"Published I2P child path changed");
        }else{require(member.namedCurrent()&&memberCapture(*impl_->lease,*target,impl_->directory)==*member.accepted,ErrorCode::Conflict,"Unprepared I2P child changed during publication");}
      }else if(!member.revoked){require(member.namedCurrent()&&memberCapture(*impl_->lease,member.hash,impl_->directory)==*member.snapshot(),ErrorCode::Conflict,"I2P publication changed sibling metadata");}
    }}
    impl_->stamp=std::move(after);impl_->refreshTrackers();impl_->dirty=false;
    // Verified publication updates currentness, never a one-shot effect grant.
    if(impl_->authority){impl_->authority->revision_=impl_->lease->sourceRevision();}
    require(!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"I2P source changed or retired after publication");return {};
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"I2P publication failed; group source revoked"});}
}
std::expected<void,storage::Error> ProfileSource::releaseChild(const core::TorrentHash& hash){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing,ErrorCode::Conflict,"I2P release requires session anchor");
    bool found=false;std::erase_if(impl_->children,[&](const auto& weak){auto child=weak.lock();if(!child){return true;}if(child->hash()==hash){child->revoke();found=true;return true;}return false;});
    require(found,ErrorCode::Conflict,"I2P child is not retained");impl_->refreshTrackers();return {};
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot release owned I2P child"});}
}
}
