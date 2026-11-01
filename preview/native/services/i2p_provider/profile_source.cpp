// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_source.hpp"
#include "mixed_i2p_source.hpp"
#include "pending_magnet_source.hpp"
#include "sam_protocol.hpp"
#include "file_io.hpp"
#include "plugin_settings.hpp"
#include "torrent_state.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/hasher.hpp>
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
std::string folded(std::string_view value){std::string out(value);for(auto& c:out){if(c>='A'&&c<='Z'){c=static_cast<char>(c+'a'-'A');}}return out;}
struct Sources {bool tracker=false,discovery=false;};
Sources sourceSelection(const core::Document& state,const core::Document& config){
  const auto* attrs=state.find("attributes");require(attrs&&unique(*attrs),ErrorCode::InvalidFormat,"Unique owned I2P attributes required");
  bool tracker=false,dht=false,trackerDenied=false,dhtDenied=false;
  const auto parse=[&](const core::Node* list,bool denied){
    if(!list){return;}require(list->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Malformed I2P peer source list");
    for(const auto& value:list->children){require(value.type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed I2P peer source");const auto name=folded(value.bytes);
      if(name=="tracker"){(denied?trackerDenied:tracker)=true;}if(name=="dht"){(denied?dhtDenied:dht)=true;}}
  };parse(attrs->find("peersources"),false);parse(attrs->find("peersourcesdenied"),true);
  const auto* enabled=config.find("Plugin.azneti2phelper.azi2phelper.dht.enable");
  const bool pluginDht=!enabled||(enabled->type==core::Node::Type::Integer&&enabled->integer==1);
  const auto* announce=state.find("announce");
  if(!announce){if(const auto* tiers=state.find("announce-list");tiers&&tiers->type==core::Node::Type::List&&!tiers->children.empty()){
    const auto& first=tiers->children.front();if(first.type==core::Node::Type::List&&!first.children.empty()){announce=&first.children.front();}}}
  require(!announce||announce->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed original I2P announce");
  const std::string_view url=announce?announce->bytes:std::string_view{};
  const auto colon=url.find(':');const bool decentralized=colon!=std::string_view::npos&&folded(url.substr(0,colon))=="dht";
  bool backup=true;if(const auto* az=state.find("azureus_properties")){require(unique(*az),ErrorCode::InvalidFormat,"Malformed original I2P properties");if(const auto* v=az->find("dht_backup_enable")){require(v->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original DHT backup policy");backup=v->integer==1;}}
  // Bounded SDK/privacy subset: reject every actual private bit for DHT.
  // Original isReallyPrivate additionally requires a passkey; that less strict
  // behavior remains explicitly unsupported while the SDK rejects priv().
  const auto* info=state.find("info");const auto* priv=info?info->find("private"):nullptr;
  require(!priv||priv->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original private flag");
  const bool reallyPrivate=priv&&priv->integer!=0;
  return {tracker&&!trackerDenied,pluginDht&&!reallyPrivate&&(decentralized||(backup&&dht&&!dhtDenied))};
}
std::string pendingKey(const core::TorrentHash& hash){constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,count=0;for(auto b:hash.bytes()){bits=(bits<<8)|std::to_integer<unsigned>(b);count+=8;while(count>=5){count-=5;out+=alphabet[(bits>>count)&31];}}if(count){out+=alphabet[(bits<<(5-count))&31];}return out;}
std::string pendingConfigInvariant(const std::string& bytes,const core::TorrentHash& hash){
  auto doc=take(core::Document::parse(bytes,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid pending source config"};}));
  require(unique(doc.root()),ErrorCode::InvalidFormat,"Unique pending source config required");
  const auto key=pendingKey(hash);std::string out="d",pending="de";
  for(std::size_t i=0;i<doc.root().children.size();i+=2){const auto& k=doc.root().children[i];const auto& v=doc.root().children[i+1];
    if(k.bytes=="MagnetPlugin.active.magnets"){require(unique(v)&&v.children.size()<=256,ErrorCode::InvalidFormat,"Unique bounded pending rows required");pending="d";for(std::size_t j=0;j<v.children.size();j+=2){require(unique(v.children[j+1]),ErrorCode::InvalidFormat,"Unique pending row required");if(v.children[j].bytes!=key){pending+=doc.originalBytes().substr(v.children[j].begin,v.children[j+1].end-v.children[j].begin);}}pending+='e';}
    else{out+=doc.originalBytes().substr(k.begin,k.end-k.begin)+doc.originalBytes().substr(v.begin,v.end-v.begin);}}
  return out+"27:MagnetPlugin.active.magnets"+pending+'e';
}
Stamp capture(const storage::ProfileLease& lease,const core::TorrentHash* hash,const std::string& directory,bool running=true,bool mixed=false){
  require(absent(lease.directoryHandle(),"biglybt.config.saving")&&absent(lease.directoryHandle(),"downloads.config.saving"),ErrorCode::Conflict,"Resolve owned I2P source recovery first");
  auto config=take(storage::ConfigFile::load(lease,"biglybt.config",SourceLimits));auto inventory=take(storage::ConfigFile::load(lease,"downloads.config",SourceLimits));
  require(config.recoverySource()==storage::RecoverySource::Primary&&inventory.recoverySource()==storage::RecoverySource::Primary&&unique(config.document().root())&&unique(inventory.document().root()),ErrorCode::Conflict,"Owned I2P requires unique primary sources");
  auto binding=storage::originalPluginBinding("azneti2phelper",directory);require(bool(binding),ErrorCode::InvalidFormat,"Invalid original I2P plugin directory");
  storage::OriginalPluginSettings settings(config.document(),std::move(*binding));
  const bool enabled=settings.loadedAtStartup()&&settings.boolean("enable",true)&&settings.boolean(mixed?"azi2phelper.mix.enabled":"azi2phelper.pure.enabled",true);
  require(enabled&&settings.wrongTypes().empty(),ErrorCode::Unsupported,mixed?"Original mixed I2P identity is disabled or malformed":"Original pure I2P identity is disabled or malformed");
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
  const auto* networks=attrs->find("networks");
  const bool pure=networks&&networks->type==core::Node::Type::List&&networks->children.size()==1&&networks->children[0].type==core::Node::Type::Bytes&&networks->children[0].bytes=="I2P";
  const bool both=networks&&networks->type==core::Node::Type::List&&networks->children.size()==2
      &&networks->children[0].type==core::Node::Type::Bytes&&networks->children[1].type==core::Node::Type::Bytes
      &&((networks->children[0].bytes=="Public"&&networks->children[1].bytes=="I2P")||(networks->children[0].bytes=="I2P"&&networks->children[1].bytes=="Public"));
  require(mixed?both:pure,ErrorCode::Unsupported,mixed?"Mixed admission requires exact saved Public and I2P networks":"Dedicated owned admission requires only saved I2P network");
  if(const auto* flags=attrs->find("flags")){require(flags->type==core::Node::Type::Integer&&(static_cast<std::uint64_t>(flags->integer)&0x200U)==0,ErrorCode::Unsupported,"Full owned I2P metadata required");}
  const auto permissions=sourceSelection(state.document(),config.document());
  require(permissions.tracker||permissions.discovery,ErrorCode::Unsupported,"Original I2P Tracker and DHT sources are unavailable");
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
Stamp memberCapture(const storage::ProfileLease& lease,const core::TorrentHash& hash,const std::string& directory,bool running=true,bool mixed=false){
  auto value=capture(lease,&hash,directory,running,mixed);value.inventory=inventoryScope(value.inventory,hash,true);return value;
}
Stamp pendingCapture(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,const std::string& directory,bool mixed){
  auto value=capture(lease,nullptr,directory,true,mixed);
  // This policy is a pure syntax projection for existing Public hints. It does
  // not create a Public session or grant any peer effect.
  engine::NetworkPolicy parsePolicy;parsePolicy.mode=engine::NetworkMode::Normal;parsePolicy.httpTrackers=true;parsePolicy.udpTrackers=true;parsePolicy.ipv6=true;
  (void)take(engine::validatePendingMagnet(lease,intent,parsePolicy));
  require(intent.network==(mixed?engine::MagnetNetwork::PublicAndI2p:engine::MagnetNetwork::I2p),ErrorCode::Unsupported,"Pending source identity/network mismatch");
  auto config=take(core::Document::parse(value.config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid pending source configuration"};}));
  const auto* enabled=config.find("Plugin.azneti2phelper.azi2phelper.dht.enable");
  const bool discovery=!enabled||(enabled->type==core::Node::Type::Integer&&enabled->integer==1);
  const bool tracker=std::any_of(intent.trackers.begin(),intent.trackers.end(),[](const auto& url){if(!url.starts_with("http://")){return false;}auto host=std::string_view(url).substr(7);host=host.substr(0,host.find_first_of("/:?#"));return bool(hashName(host));});
  require(discovery||tracker,ErrorCode::Unsupported,"Pending I2P metadata has neither current DHT nor explicit I2P tracker source");
  const auto* map=config.find("MagnetPlugin.active.magnets");require(map&&unique(*map),ErrorCode::Conflict,"Original pending magnet map required");
  const auto* row=map->find(pendingKey(intent.hash));require(row&&unique(*row),ErrorCode::Conflict,"Original pending magnet row required");
  const auto raw=config.originalBytes().substr(row->begin,row->end-row->begin);
  require(!intent.sourceEntry.empty()&&raw==intent.sourceEntry,ErrorCode::Conflict,"Original pending magnet intent changed");
  value.inventory=inventoryScope(value.inventory,intent.hash,true);value.state=raw;return value;
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
  bool session=false,publishing=false,mixed=false;
  std::optional<core::TorrentHash> publicationTarget;
  std::shared_ptr<const engine::MixedManagedSource> managed;
  std::optional<engine::PendingMagnet> pending;
  std::shared_ptr<const engine::PendingMetadataSource> metadata;
  std::atomic_bool discovery{false};
  Stamp memberSnapshot(bool running=true)const{return pending?pendingCapture(*lease,*pending,directory,mixed):memberCapture(*lease,hash,directory,running,mixed);}
  bool managedCurrent()const noexcept{return pending?(!stamp||(metadata&&metadata->effectsCurrent())):!mixed||session||(managed&&managed->current());}
  const char* keyName()const noexcept{return mixed?"dest_key.dat":"dest_key1.dat";}
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
      &&::fstat(key.get(),&e)==0&&sameFile(keyStat,e)&&::fstatat(plugin.get(),keyName(),&f,AT_SYMLINK_NOFOLLOW)==0&&sameFile(e,f);
  }
};
ProfileSource::ProfileSource(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
ProfileSource::~ProfileSource()=default;
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::open(const storage::ProfileLease& caller,const core::TorrentHash& hash,std::string directory){return openImpl(caller,hash,std::move(directory),false);}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::createSession(const storage::ProfileLease& caller,std::string directory){return openImpl(caller,core::TorrentHash{},std::move(directory),true);}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::createMixedSession(const storage::ProfileLease& caller,std::string directory){return openImpl(caller,core::TorrentHash{},std::move(directory),true,true);}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::openImpl(const storage::ProfileLease& caller,const core::TorrentHash& hash,std::string directory,bool session,bool mixed){
  try{
    auto retained=take(caller.retain());const auto& held=*retained;auto access=take(held.sourceAccess());
    auto binding=storage::originalPluginBinding("azneti2phelper",directory);require(bool(binding),ErrorCode::UnsafePath,"Invalid I2P plugin basename");
    auto impl=std::make_unique<Impl>(retained,hash,std::move(directory),engine::OwnedI2pPolicy(mixed?std::shared_ptr<const storage::ProfileLease>{}:retained,hash));impl->mixed=mixed;
    require(::fstat(held.directoryHandle(),&impl->profileStat)==0&&S_ISDIR(impl->profileStat.st_mode)&&impl->profileStat.st_uid==::geteuid(),ErrorCode::UnsafePath,"Owned profile directory required");
    impl->plugins=Descriptor(::openat(held.directoryHandle(),"plugins",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    require(impl->plugins.get()>=0&&::fstat(impl->plugins.get(),&impl->pluginsStat)==0,ErrorCode::UnsafePath,"Existing held plugin directories required");
    impl->plugin=Descriptor(::openat(impl->plugins.get(),impl->directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    require(impl->plugin.get()>=0&&::fstat(impl->plugin.get(),&impl->pluginStat)==0,ErrorCode::UnsafePath,"Existing original I2P plugin directory required");
    impl->key=Descriptor(::openat(impl->plugin.get(),impl->keyName(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    require(impl->key.get()>=0&&::fstat(impl->key.get(),&impl->keyStat)==0&&S_ISREG(impl->keyStat.st_mode)&&impl->keyStat.st_uid==::geteuid()&&impl->keyStat.st_nlink==1&&(impl->keyStat.st_mode&0077)==0&&impl->keyStat.st_size>0&&impl->keyStat.st_size<=static_cast<off_t>(MaximumClientIdentityInputBytes),ErrorCode::UnsafePath,mixed?"Private original mixed identity key required":"Private original pure identity key required");
    std::array<Byte,MaximumClientIdentityInputBytes> bytes{};struct Wipe{decltype(bytes)& value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}} wipe{bytes};
    const auto size=static_cast<std::size_t>(impl->keyStat.st_size);std::size_t done=0;
    while(done<size){const auto n=::pread(impl->key.get(),bytes.data()+done,size-done,static_cast<off_t>(done));if(n<0&&errno==EINTR){continue;}require(n>0,ErrorCode::Io,"Cannot read original I2P identity");done+=static_cast<std::size_t>(n);}
    auto identity=decodeClientIdentity(View(bytes).first(size));require(bool(identity),ErrorCode::Unsupported,"Unsupported or invalid original I2P identity");
    impl->publicIdentity=*identity->destination();impl->identity.emplace(std::move(*identity));
    impl->session=session;
    auto initial=capture(held,session?nullptr:&hash,impl->directory,true,impl->mixed);
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
bool ProfileSource::mixedMode()const noexcept{return impl_->mixed;}
void ProfileSource::invalidate()noexcept{impl_->dirty=true;if(impl_->anchor){impl_->anchor->impl_->dirty=true;}}
const Destination& ProfileSource::destination()const noexcept{return impl_->publicIdentity;}
const engine::OwnedI2pPolicy& ProfileSource::policy()const noexcept{return impl_->policy;}
const core::TorrentHash& ProfileSource::hash()const noexcept{return impl_->hash;}
bool ProfileSource::discoveryAllowed()const noexcept{return prepared()&&impl_->discovery.load(std::memory_order_acquire)&&(!impl_->anchor||impl_->anchor->prepared());}
bool ProfileSource::prepared()const noexcept{return impl_->stamp.has_value()&&!impl_->revoked&&impl_->managedCurrent();}
void ProfileSource::revoke()noexcept{impl_->revoked=true;if(impl_->authority){impl_->authority->revoke();}}
std::shared_ptr<i2cp::EffectAuthority> ProfileSource::effectAuthority()const noexcept{return impl_->authority;}
bool ProfileSource::revisionCurrent(std::uint64_t revision)const noexcept{return !impl_->revoked&&!impl_->dirty&&impl_->managedCurrent()&&(!impl_->anchor||(!impl_->anchor->impl_->revoked&&!impl_->anchor->impl_->dirty))&&impl_->lease->sourceRevision()==revision;}
bool ProfileSource::trackerAllowed(const Hash& hash,std::uint16_t port)const noexcept{
  if(impl_->revoked){return false;}if(impl_->session){auto grants=impl_->trackerGrants.load();if(!grants){return false;}
    return std::any_of(grants->begin(),grants->end(),[&](const auto& grant){auto child=grant.child.lock();return child&&!child->impl_->revoked&&child->impl_->managedCurrent()&&grant.hash==hash&&grant.port==port;});}
  return impl_->managedCurrent()&&std::find(impl_->trackers.begin(),impl_->trackers.end(),std::pair{hash,port})!=impl_->trackers.end();
}
std::expected<ClientIdentity,storage::Error> ProfileSource::takeIdentity(){
  auto valid=validate();if(!valid){return std::unexpected(valid.error());}
  if(!impl_->identity){return std::unexpected(Error{ErrorCode::Conflict,"Original I2P identity already consumed"});}
  ClientIdentity result=std::move(*impl_->identity);impl_->identity.reset();return result;
}
std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorage(){
  if(impl_->pending){return std::unexpected(Error{ErrorCode::Unsupported,"Pending magnet has no payload storage authority"});}
  if(impl_->mixed){return std::unexpected(Error{ErrorCode::Unsupported,"Mixed source borrows the sole engine storage admission"});}
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
    if(impl_->anchor){require(impl_->accepted&&impl_->memberSnapshot(true)==*impl_->accepted,ErrorCode::Conflict,"I2P child source changed after acceptance");}
    else{static_cast<void>(capture(*impl_->lease,&impl_->hash,impl_->directory,true,impl_->mixed));}
    auto before=impl_->anchor?impl_->memberSnapshot():capture(*impl_->lease,&impl_->hash,impl_->directory,true,impl_->mixed);
    auto beforeState=take(core::Document::parse(before.state,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid I2P source state"};}));
    auto beforeConfig=take(core::Document::parse(before.config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid I2P source config"};}));
    impl_->policy.trackers_=sourceSelection(beforeState,beforeConfig).tracker;
    if(impl_->anchor&&std::ranges::any_of(impl_->anchor->impl_->children,[](const auto& weak){const auto child=weak.lock();return child&&child->impl_->pending&&(!child->impl_->metadata||child->impl_->metadata->current());})){
      // Alias exclusion hashes the ORIGINAL info bytes. Do not send original
      // Java UTF16 v2 file-tree order through an extra SDK metadata parser.
      const auto rawInfo=take(storage::originalTorrentInfoSubject(beforeState));const auto* info=beforeState.find("info");const auto* version=info->find("meta version");
      const bool v2=version&&version->type==core::Node::Type::Integer&&version->integer==2;const bool v1=!v2||info->find("pieces");
      const auto sha1=libtorrent::hasher(rawInfo.data(),static_cast<int>(rawInfo.size())).final();const auto sha256=libtorrent::hasher256(rawInfo.data(),static_cast<int>(rawInfo.size())).final();
      for(const auto& weak:impl_->anchor->impl_->children){if(auto child=weak.lock();child&&child->impl_->pending&&(!child->impl_->metadata||child->impl_->metadata->current())){const auto& requested=child->impl_->pending->requested;
        require(!(requested.has_v1()&&v1&&requested.v1==sha1)&&!(requested.has_v2()&&v2&&requested.v2==sha256),ErrorCode::Conflict,"Retire actual metadata alias before payload preparation");
      }}
    }
    auto storage=take(engine::detail::ManagedStorage::prepareOwnedI2p(*impl_->lease,impl_->hash,impl_->policy,impl_->claimed,impl_->retainedIdentities));
    std::vector<std::string> urls=storage.parameters().trackers;for(const auto& entry:storage.parameters().ti->trackers()){if(std::find(urls.begin(),urls.end(),entry.url)==urls.end()){urls.push_back(entry.url);}}
    std::erase_if(urls,[](const auto& url){return folded(url).starts_with("dht://");});require(urls.size()<=4,ErrorCode::Unsupported,"At most four exact I2P HTTP trackers supported");
    for(const auto& url:urls){require(url.starts_with("http://"),ErrorCode::Unsupported,"Exact I2P HTTP tracker required");std::string_view authority(url);authority.remove_prefix(7);authority=authority.substr(0,authority.find_first_of("/?#"));auto colon=authority.find(':');auto hash=hashName(authority.substr(0,colon));auto port=colon==std::string_view::npos?std::optional<std::uint64_t>(80):decimal(authority.substr(colon+1),65535);require(hash&&port&&*port,ErrorCode::Unsupported,"Exact numeric I2P tracker port/hash required");impl_->trackers.emplace_back(*hash,static_cast<std::uint16_t>(*port));}
    if(impl_->active.get()<0){
      impl_->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
      require(impl_->active.get()>=0&&::fstat(impl_->active.get(),&impl_->activeStat)==0,ErrorCode::UnsafePath,"Held original active metadata directory required");
    }
    if(!impl_->anchor){impl_->bind(impl_->lease->directoryHandle(),".azlock");impl_->bind(impl_->lease->directoryHandle(),"biglybt.config");impl_->bind(impl_->lease->directoryHandle(),"downloads.config");}
    impl_->bind(impl_->active.get(),impl_->hash.hex()+".dat");
    impl_->stamp=impl_->anchor?impl_->memberSnapshot(true):capture(*impl_->lease,&impl_->hash,impl_->directory,true,impl_->mixed);
    require(impl_->namedCurrent(),ErrorCode::Conflict,"Owned I2P source changed during storage preparation");
    if(impl_->anchor){impl_->admissionClaims.assign(storage.claims().begin(),storage.claims().end());impl_->admissionIdentity=storage.identity();}
    auto actualState=take(core::Document::parse(impl_->stamp->state,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid prepared I2P state"};}));
    auto actualConfig=take(core::Document::parse(impl_->stamp->config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid prepared I2P config"};}));
    const auto permissions=sourceSelection(actualState,actualConfig);impl_->discovery=permissions.discovery;if(!permissions.tracker){impl_->trackers.clear();}
    const std::weak_ptr<ProfileSource> source=shared_from_this();impl_->authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([source](std::uint64_t revision){auto held=source.lock();return held&&held->revisionCurrent(revision);},impl_->lease->sourceRevision()));return storage;
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"Owned I2P storage preparation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validate()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    require(!impl_->revoked,ErrorCode::Conflict,"Owned I2P source is revoked");
    require(!impl_->stamp||impl_->managedCurrent(),ErrorCode::Conflict,"Mixed engine admission is no longer current");
    if(impl_->anchor){
      auto valid=impl_->anchor->validate();if(!valid){return std::unexpected(valid.error());}
      require(impl_->namedCurrent(),ErrorCode::Conflict,"I2P child metadata path changed");
      const auto now=impl_->memberSnapshot(true);require(!impl_->snapshot()||now==*impl_->snapshot(),ErrorCode::Conflict,"I2P selected source changed");
    }else{
      require(impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P identity source is no longer current");
      const auto now=capture(*impl_->lease,impl_->session?nullptr:&impl_->hash,impl_->directory,true,impl_->mixed);
      require(!impl_->stamp||now==*impl_->stamp,ErrorCode::Conflict,"Owned I2P profile source changed");
      if(impl_->session){for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&!child->impl_->revoked&&child->impl_->snapshot()){
        require(child->impl_->namedCurrent()&&child->impl_->memberSnapshot(true)==*child->impl_->snapshot(),ErrorCode::Conflict,"Active I2P child source changed");}}}
    }
    require(impl_->namedCurrent(),ErrorCode::Conflict,"Original I2P source changed while reading");impl_->dirty=false;return {};
  }catch(const Rejected& error){const_cast<ProfileSource*>(this)->revoke();return std::unexpected(error.error);}catch(...){const_cast<ProfileSource*>(this)->revoke();return std::unexpected(Error{ErrorCode::Io,"Owned I2P source validation failed"});}
}
std::expected<void,storage::Error> ProfileSource::validateTransportSnapshot()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    // Transport close revokes effects before reporting why it closed. Inspect
    // the held source independently; this never blesses a revoked generation.
    const auto& root=impl_->anchor?*impl_->anchor->impl_:*impl_;
    require(root.namedCurrent()&&root.snapshot(),ErrorCode::Conflict,"I2P transport source no longer names its accepted snapshot");
    const auto common=capture(*root.lease,root.session?nullptr:&root.hash,root.directory,false,root.mixed);
    require(common==*root.snapshot(),ErrorCode::Conflict,"I2P transport profile source changed");
    if(root.session){for(const auto& weak:root.children){if(auto child=weak.lock();child&&child->impl_->snapshot()&&!(child->impl_->pending&&child->impl_->revoked)){
      require(child->impl_->namedCurrent()&&child->impl_->memberSnapshot(false)==*child->impl_->snapshot(),ErrorCode::Conflict,"I2P transport member source changed");
    }}}
    if(impl_->anchor){require(impl_->namedCurrent()&&impl_->snapshot()
        &&impl_->memberSnapshot(false)==*impl_->snapshot(),ErrorCode::Conflict,"I2P transport selected source changed");}
    require(root.namedCurrent(),ErrorCode::Conflict,"I2P transport source changed while reading");return {};
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot inspect withdrawn I2P transport source"});}
}
std::expected<void,storage::Error> ProfileSource::validateRetiredSnapshot()const{
  try{auto alias=take(impl_->lease->retain());auto access=take(alias->sourceAccess());
    require(impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source no longer names its exact prepared snapshot");
    if(impl_->anchor){auto valid=impl_->anchor->validate();if(!valid){return std::unexpected(valid.error());}}
    const auto now=impl_->anchor?impl_->memberSnapshot(false):capture(*impl_->lease,&impl_->hash,impl_->directory,true,impl_->mixed);
    require(now==*impl_->stamp&&impl_->namedCurrent(),ErrorCode::Conflict,"Retired I2P source changed before checkpoint");
    return {}; // Read-only; intentionally never restores revoked effects.
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate retired I2P source snapshot"});}
}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::prepareChild(const core::TorrentHash& hash,
    std::span<const engine::detail::ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> identities){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->mixed&&!impl_->publishing,ErrorCode::Conflict,"Pure I2P child requires its pure session anchor");
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
    auto accepted=memberCapture(*impl_->lease,hash,impl_->directory,true,impl_->mixed);
    auto data=std::make_unique<Impl>(impl_->lease,hash,impl_->directory,engine::OwnedI2pPolicy(impl_->lease,hash));
    data->anchor=shared_from_this();data->publicIdentity=impl_->publicIdentity;data->accepted=std::move(accepted);
    data->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
    require(data->active.get()>=0&&::fstat(data->active.get(),&data->activeStat)==0,ErrorCode::UnsafePath,"Held I2P child metadata directory required");
    data->bind(data->active.get(),hash.hex()+".dat");require(data->namedCurrent(),ErrorCode::Conflict,"I2P child changed while accepting");
    data->claimed.assign(claimed.begin(),claimed.end());data->retainedIdentities.assign(identities.begin(),identities.end());
    auto child=std::shared_ptr<ProfileSource>(new ProfileSource(std::move(data)));impl_->children.push_back(child);return child;
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare owned I2P child"});}
}
std::expected<std::shared_ptr<ProfileSource>,storage::Error> ProfileSource::prepareMixedChild(const core::TorrentHash& hash){
  try{auto access=take(impl_->lease->sourceAccess());
    require(impl_->session&&impl_->mixed&&!impl_->publishing,ErrorCode::Conflict,"Mixed child requires its mixed session anchor");
    auto current=validate();if(!current){return std::unexpected(current.error());}
    std::erase_if(impl_->children,[](const auto& weak){return weak.expired();});
    require(impl_->children.size()<32,ErrorCode::Unsupported,"I2P session member bound");
    for(const auto& weak:impl_->children){if(auto child=weak.lock()){require(child->hash()!=hash,ErrorCode::Conflict,"Mixed child already retained");}}
    auto accepted=memberCapture(*impl_->lease,hash,impl_->directory,true,true);
    // Deliberately no OwnedI2pPolicy mint and no payload/path preparation.
    auto data=std::make_unique<Impl>(impl_->lease,hash,impl_->directory,engine::OwnedI2pPolicy({},hash));
    data->mixed=true;data->anchor=shared_from_this();data->publicIdentity=impl_->publicIdentity;data->accepted=std::move(accepted);
    data->active=Descriptor(::openat(impl_->lease->directoryHandle(),"active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_DIRECTORY));
    require(data->active.get()>=0&&::fstat(data->active.get(),&data->activeStat)==0,ErrorCode::UnsafePath,"Held mixed metadata directory required");
    data->bind(data->active.get(),hash.hex()+".dat");require(data->namedCurrent(),ErrorCode::Conflict,"Mixed child changed while accepting");
    auto child=std::shared_ptr<ProfileSource>(new ProfileSource(std::move(data)));impl_->children.push_back(child);return child;
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot accept mixed I2P metadata source"});}
}
std::expected<void,storage::Error> ProfileSource::bindMixedManaged(std::shared_ptr<const engine::MixedManagedSource> managed){
  try{auto access=take(impl_->lease->sourceAccess());
    require(impl_->mixed&&!impl_->session&&impl_->anchor&&!impl_->revoked&&!impl_->stamp&&!impl_->managed&&impl_->accepted,
        ErrorCode::Conflict,"Mixed engine binding is one-shot");
    auto& root=*impl_->anchor->impl_;
    require(root.mixed&&root.publishing&&root.publicationTarget&&*root.publicationTarget==impl_->hash&&!root.revoked,
        ErrorCode::Conflict,"Mixed binding requires its actual target storage publication");
    require(managed&&managed->current()&&managed->admission()&&managed->lease().sharesOwnership(*impl_->lease)
        &&managed->identity().originalKey==impl_->hash,ErrorCode::Conflict,"Actual same-profile mixed engine admission required");
    auto selected=storage::validateOriginalTorrentKey(managed->identity(),impl_->hash);require(bool(selected),ErrorCode::Conflict,"Mixed engine full identity does not match saved selection");
    auto now=memberCapture(*impl_->lease,impl_->hash,impl_->directory,true,true);
    require(now.config==impl_->accepted->config&&now.state==managed->stateSnapshot(),ErrorCode::Conflict,"Mixed metadata differs from actual prepared engine source");
    require(managed->claims().size()<=engine::detail::MaximumManagedPayloadClaims,ErrorCode::Unsupported,"Mixed admission payload claim bound");
    std::vector<engine::detail::ManagedPayloadClaim> claims(managed->claims().begin(),managed->claims().end());
    std::vector<std::pair<Hash,std::uint16_t>> trackers;
    for(const auto& url:managed->trackers()){
      const auto scheme=url.find("://");if(scheme==std::string::npos){continue;}
      std::string_view authority(url);authority.remove_prefix(scheme+3);authority=authority.substr(0,authority.find_first_of("/?#"));
      const auto colon=authority.find(':');const auto host=authority.substr(0,colon);
      if(!host.ends_with(".b32.i2p")){continue;} // Public endpoints never enter the I2P grant set.
      require(url.starts_with("http://")&&url.size()<=2048&&url.find_first_of("\r\n\t ")==std::string::npos&&url.find('\0')==std::string::npos,
          ErrorCode::Unsupported,"Mixed I2P tracker requires bounded original HTTP routing");
      const auto hash=hashName(host);const auto port=colon==std::string_view::npos?std::optional<std::uint64_t>(80):decimal(authority.substr(colon+1),65535);
      require(hash&&port&&*port,ErrorCode::Unsupported,"Mixed I2P tracker requires exact b32 hash/numeric port");
      const auto grant=std::pair{*hash,static_cast<std::uint16_t>(*port)};
      if(std::find(trackers.begin(),trackers.end(),grant)==trackers.end()){trackers.push_back(grant);}
      require(trackers.size()<=4,ErrorCode::Unsupported,"Mixed I2P tracker grant bound");
    }
    const std::weak_ptr<ProfileSource> source=shared_from_this();
    auto authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([source](std::uint64_t revision){auto held=source.lock();return held&&held->revisionCurrent(revision);},impl_->lease->sourceRevision()));
    // Re-open only the target's actual postprepare source. The surrounding
    // publication independently checks config/key, inventory and all siblings.
    impl_->bind(impl_->active.get(),impl_->hash.hex()+".dat");
    require(impl_->namedCurrent()&&memberCapture(*impl_->lease,impl_->hash,impl_->directory,true,true)==now
        &&managed->current()&&!impl_->revoked&&!root.revoked,ErrorCode::Conflict,"Mixed admission changed before binding");
    auto stateDoc=take(core::Document::parse(now.state,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid mixed state"};}));
    auto configDoc=take(core::Document::parse(now.config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid mixed config"};}));
    const auto permissions=sourceSelection(stateDoc,configDoc);impl_->discovery=permissions.discovery;if(!permissions.tracker){trackers.clear();}
    impl_->admissionIdentity=managed->identity();impl_->admissionClaims=std::move(claims);impl_->trackers=std::move(trackers);
    impl_->managed=std::move(managed);impl_->stamp=std::move(now);impl_->authority=std::move(authority);return {};
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"Cannot bind actual mixed managed source"});}
}
std::expected<void,storage::Error> ProfileSource::releaseMixedChild(const core::TorrentHash& hash){
  if(!impl_->mixed||!impl_->session){return std::unexpected(Error{ErrorCode::Unsupported,"Mixed release requires its mixed session anchor"});}
  return releaseChild(hash);
}
std::expected<void,storage::Error> ProfileSource::publish(const core::TorrentHash& target,
    const std::function<std::expected<void,storage::Error>()>& operation){return publishImpl(target,operation);}
std::expected<void,storage::Error> ProfileSource::publishPositions(
    const std::function<std::expected<void,storage::Error>()>& operation){return publishImpl({},operation);}
std::expected<void,storage::Error> ProfileSource::publishImpl(const std::optional<core::TorrentHash>& target,
    const std::function<std::expected<void,storage::Error>()>& operation,bool pendingPublication){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing&&bool(operation),ErrorCode::Conflict,"I2P publication requires one nonreentrant anchor operation");
    auto valid=validate();if(!valid){return std::unexpected(valid.error());}
    require(!pendingPublication||target.has_value(),ErrorCode::Conflict,"Pending publication requires one target");
    const auto configScope=pendingPublication?pendingConfigInvariant(impl_->stamp->config,*target):impl_->stamp->config;
    const auto others=target?inventoryScope(impl_->stamp->inventory,*target,false):positionInvariant(impl_->stamp->inventory);
    struct BeforeName {int directory;std::string name;struct stat value;};
    struct BeforeMember {std::shared_ptr<ProfileSource> source;Stamp stamp;std::vector<BeforeName> names;};
    std::vector<BeforeMember> members;
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&!(child->impl_->pending&&child->impl_->revoked)){
      BeforeMember before{child,child->impl_->memberSnapshot(false),{}};
      for(const auto& bound:child->impl_->bound){before.names.push_back({bound.directory,bound.name,bound.stat});}members.push_back(std::move(before));}}
    impl_->publishing=true;impl_->publicationTarget=target;
    struct Reset {Impl& value;~Reset(){value.publishing=false;value.publicationTarget.reset();}} reset{*impl_};
    auto result=operation();if(!result){
      // A rejected command may advance the cooperative epoch before any write.
      // Independent before-images prevent a changed target from blessing itself
      // by updating its own stored snapshot in the callback.
      bool unchanged=impl_->namedCurrent()&&capture(*impl_->lease,nullptr,impl_->directory,true,impl_->mixed)==*impl_->stamp;
      for(const auto& before:members){
        unchanged=unchanged&&before.source->impl_->namedCurrent()
            &&before.source->impl_->memberSnapshot(false)==before.stamp;
        for(const auto& file:before.names){struct stat named{};unchanged=unchanged&&::fstatat(file.directory,file.name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameFile(file.value,named);}
      }
      if(!unchanged){revoke();}else{impl_->dirty=false;if(impl_->authority){impl_->authority->revision_=impl_->lease->sourceRevision();}}
      return std::unexpected(result.error());
    }
    require(!impl_->revoked,ErrorCode::Cancelled,"I2P group retired during publication");
    auto after=capture(*impl_->lease,nullptr,impl_->directory,true,impl_->mixed);
    require((pendingPublication?pendingConfigInvariant(after.config,*target):after.config)==configScope&&(target?inventoryScope(after.inventory,*target,false):positionInvariant(after.inventory,true))==others,ErrorCode::Conflict,"I2P publication changed unrelated source rows");
    // Immutable common named files remain exact. Only downloads.config may have
    // been durably replaced by this actual trusted operation.
    auto inventory=std::find_if(impl_->bound.begin(),impl_->bound.end(),[](const auto& file){return file.name=="downloads.config";});
    require(inventory!=impl_->bound.end(),ErrorCode::Conflict,"I2P inventory binding unavailable");
    impl_->bound.erase(inventory);
    if(pendingPublication){std::erase_if(impl_->bound,[](const auto& f){return f.name=="biglybt.config";});}
    require(impl_->namedCurrent(),ErrorCode::Conflict,"I2P shared identity/config changed during publication");
    impl_->bind(impl_->lease->directoryHandle(),"downloads.config");
    if(pendingPublication){impl_->bind(impl_->lease->directoryHandle(),"biglybt.config");}
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&child->impl_->snapshot()){
      auto& member=*child->impl_;
      if(pendingPublication){
        // Rebase only the independently constrained shared config bytes. A
        // completed/cancelled pending child is revoked, never turned payload.
        if(member.pending&&member.hash==*target){
          auto config=take(core::Document::parse(after.config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published pending map"};}));
          const auto* map=config.find("MagnetPlugin.active.magnets");const auto* row=map?map->find(pendingKey(*target)):nullptr;
          if(!row){child->revoke();continue;}
          require(unique(*row)&&raw(config,*row)==member.pending->sourceEntry,ErrorCode::Conflict,"Active pending intent cannot be rewritten");
        }
        if(member.stamp){member.stamp->config=after.config;}if(member.accepted){member.accepted->config=after.config;}
      }
      if(member.pending){
        if(!member.revoked){require(member.namedCurrent()&&member.memberSnapshot(false)==*member.snapshot(),ErrorCode::Conflict,"Pending publication changed a retained intent");}continue;
      }
      if(!target){
        auto now=member.memberSnapshot(false);const auto& before=*member.snapshot();
        require(member.namedCurrent()&&now.config==before.config&&now.state==before.state
            &&positionInvariant(now.inventory,true)==positionInvariant(before.inventory),ErrorCode::Conflict,"I2P position publication changed child state");
        if(member.stamp){member.stamp->inventory=std::move(now.inventory);}else{member.accepted->inventory=std::move(now.inventory);}
      }else if(member.hash==*target){
        auto rows=take(core::Document::parse(after.inventory,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published I2P inventory"};}));
        auto saved=take(core::readLibrary(rows).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published I2P rows"};}));
        const bool started=std::any_of(saved.downloads.begin(),saved.downloads.end(),[&](const auto& row){return row.hash&&*row.hash==*target&&row.persistent&&row.startup!=core::StartupState::Stopped;})
            &&std::none_of(saved.paused.begin(),saved.paused.end(),[&](const auto& pause){return pause.hash==*target;});
        if(!started){child->revoke();}else if(member.stamp){
          member.stamp=memberCapture(*impl_->lease,*target,impl_->directory,true,member.mixed);
          auto state=take(core::Document::parse(member.stamp->state,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published source state"};}));
          auto config=take(core::Document::parse(member.stamp->config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid published source config"};}));
          const auto permissions=sourceSelection(state,config);member.discovery=permissions.discovery;if(!permissions.tracker){member.trackers.clear();}
          member.bind(member.active.get(),target->hex()+".dat");require(member.namedCurrent(),ErrorCode::Conflict,"Published I2P child path changed");
        }else{require(member.namedCurrent()&&memberCapture(*impl_->lease,*target,impl_->directory,true,member.mixed)==*member.accepted,ErrorCode::Conflict,"Unprepared I2P child changed during publication");}
      }else if(!member.revoked){require(member.namedCurrent()&&member.memberSnapshot(true)==*member.snapshot(),ErrorCode::Conflict,"I2P publication changed sibling metadata");}
    }}
    impl_->stamp=std::move(after);impl_->refreshTrackers();impl_->dirty=false;
    // Verified publication updates currentness, never a one-shot effect grant.
    if(impl_->authority){impl_->authority->revision_=impl_->lease->sourceRevision();}
    require(!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"I2P source changed or retired after publication");return {};
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"I2P publication failed; group source revoked"});}
}
std::expected<void,storage::Error> ProfileSource::publishPendingMagnet(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& operation){return publishImpl(hash,operation,true);}
std::expected<std::shared_ptr<PendingMagnetSource>,storage::Error> ProfileSource::preparePendingMagnet(const engine::PendingMagnet& intent){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing,ErrorCode::Conflict,"Pending source requires an idle session anchor");auto valid=validate();if(!valid){return std::unexpected(valid.error());}
    require(!intent.sourceEntry.empty()&&intent.destination&&intent.timeoutMillis>0&&intent.timeoutMillis<=3600000,ErrorCode::Unsupported,"Persisted bounded pending intent required");
    std::erase_if(impl_->children,[](const auto& child){return child.expired();});require(impl_->children.size()<32,ErrorCode::Unsupported,"I2P session member bound");
    for(const auto& weak:impl_->children){if(auto child=weak.lock()){require(child->hash()!=intent.hash,ErrorCode::Conflict,"I2P source identity already retained");}}
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&child->impl_->pending){const auto& other=child->impl_->pending->requested;
      require(!(intent.requested.has_v1()&&other.has_v1()&&intent.requested.v1==other.v1)&&!(intent.requested.has_v2()&&other.has_v2()&&intent.requested.v2==other.v2),ErrorCode::Conflict,"Pending full-hash alias already retained");
    }}
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&child->impl_->admissionIdentity){const auto& full=child->impl_->admissionIdentity->full;
      const bool v1=intent.requested.has_v1()&&full.v1&&std::equal(full.v1->begin(),full.v1->end(),intent.requested.v1.begin(),[](std::byte a,unsigned char b){return std::to_integer<unsigned char>(a)==b;});
      const bool v2=intent.requested.has_v2()&&full.v2&&std::equal(full.v2->begin(),full.v2->end(),intent.requested.v2.begin(),[](std::byte a,unsigned char b){return std::to_integer<unsigned char>(a)==b;});
      require(!v1&&!v2,ErrorCode::Conflict,"Pending metadata aliases an actual retained payload admission");
    }}
    auto now=pendingCapture(*impl_->lease,intent,impl_->directory,impl_->mixed);
    auto data=std::make_unique<Impl>(impl_->lease,intent.hash,impl_->directory,engine::OwnedI2pPolicy({},intent.hash));
    data->pending=intent;data->mixed=impl_->mixed;data->anchor=shared_from_this();data->publicIdentity=impl_->publicIdentity;data->accepted=std::move(now);
    auto config=take(core::Document::parse(data->accepted->config,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid pending DHT configuration"};}));
    const auto* enabled=config.find("Plugin.azneti2phelper.azi2phelper.dht.enable");data->discovery=!enabled||(enabled->type==core::Node::Type::Integer&&enabled->integer==1);
    for(const auto& url:intent.trackers){if(!url.starts_with("http://")){continue;}auto authority=std::string_view(url).substr(7);authority=authority.substr(0,authority.find_first_of("/?#"));const auto colon=authority.find(':');const auto hash=hashName(authority.substr(0,colon));if(!hash){continue;}const auto port=colon==std::string_view::npos?std::optional<std::uint64_t>{80}:decimal(authority.substr(colon+1),65535);require(port&&*port,ErrorCode::Unsupported,"Pending I2P tracker port malformed");data->trackers.emplace_back(*hash,static_cast<std::uint16_t>(*port));}
    auto source=std::shared_ptr<ProfileSource>(new ProfileSource(std::move(data)));impl_->children.push_back(source);
    return std::shared_ptr<PendingMagnetSource>(new PendingMagnetSource(std::move(source)));
  }catch(const Rejected& e){return std::unexpected(e.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare original pending I2P source"});}
}
const std::shared_ptr<const storage::ProfileLease>& PendingMagnetSource::lease()const noexcept{return source_->lease();}
const core::TorrentHash& PendingMagnetSource::hash()const noexcept{return source_->hash();}
const engine::PendingMagnet& PendingMagnetSource::intent()const noexcept{return *source_->impl_->pending;}
bool PendingMagnetSource::prepared()const noexcept{return source_->prepared();}
bool PendingMagnetSource::discoveryAllowed()const noexcept{return source_->discoveryAllowed();}
bool PendingMagnetSource::revisionCurrent(std::uint64_t value)const noexcept{return source_->revisionCurrent(value);}
std::shared_ptr<i2cp::EffectAuthority> PendingMagnetSource::effectAuthority()const noexcept{return source_->effectAuthority();}
std::expected<void,storage::Error> PendingMagnetSource::validate()const{return source_->validate();}
std::expected<void,storage::Error> PendingMagnetSource::validateTransportSnapshot()const{return source_->validateTransportSnapshot();}
std::expected<void,storage::Error> PendingMagnetSource::release(){return source_->impl_->anchor->releasePendingChild(hash());}
void PendingMagnetSource::revoke()noexcept{source_->revoke();}
std::expected<void,storage::Error> PendingMagnetSource::bindMetadata(std::shared_ptr<const engine::PendingMetadataSource> metadata){return source_->bindPendingMetadata(std::move(metadata));}
std::expected<void,storage::Error> ProfileSource::bindPendingMetadata(std::shared_ptr<const engine::PendingMetadataSource> metadata){
  auto& s=*impl_;
  try{auto access=take(s.lease->sourceAccess());require(s.pending&&s.anchor&&!s.stamp&&!s.metadata&&!s.revoked,ErrorCode::Conflict,"Pending metadata binding is one-shot");
    auto& root=*s.anchor->impl_;require(root.publishing&&root.publicationTarget&&*root.publicationTarget==s.hash&&!root.revoked,ErrorCode::Conflict,"Pending binding requires its selected source publication");
    require(metadata&&metadata->current()&&metadata->admission()&&metadata->lease().sharesOwnership(*s.lease)&&metadata->hash()==s.hash&&metadata->requested()==s.pending->requested&&metadata->intent().sourceEntry==s.pending->sourceEntry&&metadata->intent().network==s.pending->network,ErrorCode::Conflict,"Actual same-profile pending metadata admission required");
    auto now=s.memberSnapshot();require(s.accepted&&now==*s.accepted,ErrorCode::Conflict,"Pending source changed before binding");
    const std::weak_ptr<ProfileSource> weak=shared_from_this();auto authority=std::shared_ptr<i2cp::EffectAuthority>(new i2cp::EffectAuthority([weak](std::uint64_t revision){const auto held=weak.lock();return held&&held->revisionCurrent(revision);},s.lease->sourceRevision()));
    s.metadata=std::move(metadata);s.stamp=std::move(now);s.authority=std::move(authority);return {};
  }catch(const Rejected& e){revoke();return std::unexpected(e.error);}catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"Cannot bind actual pending metadata source"});}
}
std::expected<void,storage::Error> ProfileSource::releasePendingChild(const core::TorrentHash& hash){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing,ErrorCode::Conflict,"Pending release requires idle anchor");
    const auto at=std::find_if(impl_->children.begin(),impl_->children.end(),[&](const auto& weak){const auto child=weak.lock();return child&&child->hash()==hash;});
    require(at!=impl_->children.end(),ErrorCode::Missing,"Pending source is not retained");const auto child=at->lock();
    require(child->impl_->pending&&(!child->impl_->metadata||!child->impl_->metadata->current()),ErrorCode::Conflict,"Actual metadata owner must retire before source release");
    child->revoke();impl_->children.erase(at);impl_->refreshTrackers();return {};
  }catch(const Rejected& e){return std::unexpected(e.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot release pending source"});}
}
std::expected<void,storage::Error> ProfileSource::releaseChild(const core::TorrentHash& hash){
  try{auto access=take(impl_->lease->sourceAccess());require(impl_->session&&!impl_->publishing,ErrorCode::Conflict,"I2P release requires session anchor");
    bool found=false;std::erase_if(impl_->children,[&](const auto& weak){auto child=weak.lock();if(!child){return true;}if(child->hash()==hash){child->revoke();found=true;return true;}return false;});
    require(found,ErrorCode::Conflict,"I2P child is not retained");impl_->refreshTrackers();return {};
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot release owned I2P child"});}
}
}
