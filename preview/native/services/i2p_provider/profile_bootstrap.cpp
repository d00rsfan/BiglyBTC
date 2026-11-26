// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_bootstrap.hpp"
#include "generate_identity.hpp"
#include "state_file_io.hpp"
#include "plugin_settings.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <vector>
#include <new>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <type_traits>
namespace biglybtc::services::i2p::provider {
namespace {
using storage::Error;using Code=storage::ErrorCode;
namespace io = storage::detail;
using Descriptor = io::StateDescriptor;
using io::StateHandle;
using io::StateStamp;
using Clock=std::chrono::steady_clock;
struct Failure {Error error;};
void require(bool ok,Code code,const char* text){if(!ok){throw Failure{{code,text}};}}
template<class T>T take(std::expected<T,Error> value){
  if(!value){throw Failure{value.error()};}
  if constexpr (!std::is_void_v<T>) { return std::move(*value); }
}
struct File { Descriptor fd; std::optional<StateStamp> stamp; };

bool namedDirectory(StateHandle parent, const std::string& name, StateHandle directory, const StateStamp& stamp) {
  const auto held = io::stateStamp(directory);
  const auto named = io::stateStampAt(parent, name);
  return held && named && *named && io::sameStateDirectory(*held, stamp)
      && io::sameStateDirectory(**named, stamp) && io::stateAccessRestricted(directory, true);
}

// Derive the legacy metadata permission exception from this retained profile.
class PrivateMetadataProfile final {
public:
  explicit PrivateMetadataProfile(const storage::ProfileLease& lease):lease_(take(lease.retain())){}
  void current() {
    (void)take(lease_->retain());
    const auto root = lease_->directoryHandle();
    const auto stamp = take(io::stateStamp(root));
    require(stamp.directory && io::stateAccessRestricted(root, false), Code::UnsafePath,
        "Legacy metadata requires an owned private profile");
#ifndef _WIN32
    require((stamp.mode & 07777) == 0700, Code::UnsafePath, "Legacy0664 metadata requires an owned0700 profile");
#endif
    if (hops_.empty()) {
      const auto path = take(io::stateDirectoryPath(root));
      const auto base = io::filesystemRoot(path);
      auto first = take(io::openFilesystemRoot(base));
      const auto firstStamp = take(io::stateStamp(first.get()));
      hops_.push_back({{}, std::move(first), firstStamp});
      const auto relative = path.lexically_relative(base);
      require(!relative.empty(), Code::UnsafePath, "Private profile directory chain is unavailable");
      if (relative != ".") {
        for (const auto& name : relative) {
          require(hops_.size() < 64, Code::UnsafePath, "Private profile directory chain is too deep");
          auto directory = take(io::openDirectoryAt(hops_.back().fd.get(), name));
          const auto signature = take(io::stateStamp(directory.get()));
          hops_.push_back({name, std::move(directory), signature});
        }
      }
    }
    for (std::size_t index = 0; index < hops_.size(); ++index) {
      const auto& hop = hops_[index];
      const auto held = take(io::stateStamp(hop.fd.get()));
      require(io::sameStateDirectory(held, hop.stamp), Code::Conflict, "Private profile directory identity changed");
      if (index) {
        const auto named = take(io::stateStampAt(hops_[index - 1].fd.get(), hop.name));
        require(named && io::sameStateDirectory(held, *named), Code::Conflict, "Named private profile directory chain changed");
      }
    }
    require(io::sameStateDirectory(hops_.back().stamp, stamp), Code::Conflict, "Named private profile directory changed");
  }
  void recheck() { if (!hops_.empty()) { current(); } }
private:
  struct Hop { std::filesystem::path name; Descriptor fd; StateStamp stamp; };
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::vector<Hop> hops_;
};

bool sameFile(const StateStamp& a, const StateStamp& b) {
  return a.regular && b.regular && a.owned && b.owned && a.links == 1 && b.links == 1
      && io::sameStateSnapshot(a, b);
}

File capture(StateHandle directory, const std::string& name, PrivateMetadataProfile* metadata = nullptr) {
  const auto named = take(io::stateStampAt(directory, name));
  if (!named) { return {}; }
  require(named->regular && named->links == 1 && named->owned, Code::UnsafePath,
      "I2P bootstrap input must be owned regular single-link data");
  auto fd = take(io::openStateWitness(directory, name, true));
  const auto held = take(io::stateStamp(fd.get()));
  require(sameFile(*named, held), Code::Conflict, "I2P bootstrap input changed while opening");
  if (!io::stateAccessRestricted(fd.get(), true)) {
#ifndef _WIN32
    require(metadata && (held.mode & 07777) == 0664, Code::UnsafePath,
        "Writable I2P input requires preserved permission review");
    metadata->current();
#else
    (void)metadata;
    require(false, Code::UnsafePath, "Writable I2P input requires preserved permission review");
#endif
  }
  return {std::move(fd), held};
}

void current(StateHandle directory, const std::string& name, const File& file) {
  const auto named = take(io::stateStampAt(directory, name));
  const bool unchanged = file.stamp ? named && sameFile(*file.stamp, *named)
      && sameFile(*file.stamp, take(io::stateStamp(file.fd.get()))) : !named;
  require(unchanged,
      Code::Conflict, "I2P bootstrap input changed; preserve pending files for review");
}

std::string read(const File& file, std::size_t limit) {
  require(file.stamp && file.stamp->size >= 0 && static_cast<std::uint64_t>(file.stamp->size) <= limit,
      Code::InvalidFormat, "I2P bootstrap input exceeds its bound");
  std::string bytes(static_cast<std::size_t>(file.stamp->size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    const auto count = io::readStateAt(file.fd.get(), bytes.data() + offset, bytes.size() - offset, offset);
    require(count > 0, Code::Io, "Cannot read held I2P bootstrap input");
    offset += static_cast<std::size_t>(count);
  }
  require(sameFile(take(io::stateStamp(file.fd.get())), *file.stamp), Code::Conflict, "I2P bootstrap input changed during read");
  return bytes;
}
struct Secret {
  std::array<Byte,MaximumClientIdentityInputBytes> bytes{};
  ~Secret(){OPENSSL_cleanse(bytes.data(),bytes.size());}
};
void budget(std::stop_token stop,Clock::time_point end){require(!stop.stop_requested()&&Clock::now()<end,Code::Cancelled,"I2P profile bootstrap cancelled before publication");}
Descriptor directory(StateHandle parent, const std::string& name) {
  auto dir = take(io::openStateDirectory(parent, name));
  require(io::stateAccessRestricted(dir.get(), true), Code::UnsafePath, "Owned I2P plugin directory required");
  return dir;
}

bool absent(StateHandle parent, const std::string& name) { return !take(io::stateStampAt(parent, name)); }

void createDirectory(StateHandle parent, const std::string& name) {
  (void)take(io::createStateDirectory(parent, name));
}
std::string_view trim(std::string_view s){const auto first=s.find_first_not_of(" \t\r\f");return first==s.npos?std::string_view{}:s.substr(first,s.find_last_not_of(" \t\r\f")-first+1);}
std::string base32(std::span<const Byte> bytes){
  constexpr std::string_view alphabet="abcdefghijklmnopqrstuvwxyz234567";std::string out;out.reserve((bytes.size()*8+4)/5);unsigned acc=0,bits=0;
  for(const auto byte:bytes){acc=(acc<<8)|byte;bits+=8;while(bits>=5){bits-=5;out+=alphabet[(acc>>bits)&31U];}}
  if(bits){out+=alphabet[(acc<<(5-bits))&31U];}return out;
}
std::pair<std::uint16_t,dht::Id> properties(std::string_view raw,const Hash& hash){
  std::map<std::string,std::string> found;
  while(!raw.empty()){
    const auto end=raw.find('\n');const auto line=trim(raw.substr(0,end));raw=end==raw.npos?std::string_view{}:raw.substr(end+1);
    if(line.empty()||line.front()=='#'||line.front()=='!'){continue;}
    // Native never rewrites existing properties. Escapes/continuations can hide
    // relevant keys; refuse ambiguous encoding rather than approximate Java.
    require(line.find('\\')==line.npos&&line.find('\0')==line.npos,Code::Unsupported,"Escaped I2P identity properties require preserved review");
    const auto split=line.find_first_of("=: \t");const auto key=line.substr(0,split);
    if(key!="port"&&key!="nid"){continue;}
    auto value=split==line.npos?std::string_view{}:trim(line.substr(split));
    if(!value.empty()&&(value.front()=='='||value.front()==':')){value=trim(value.substr(1));}
    require(!value.empty()&&found.emplace(std::string(key),std::string(value)).second,Code::Conflict,"Ambiguous I2P identity properties");
  }
  require(found.contains("port")&&found.contains("nid"),Code::Conflict,"Incomplete original I2P DHT identity requires review");
  unsigned port=0;const auto& p=found.at("port");const auto parsed=std::from_chars(p.data(),p.data()+p.size(),port);
  require(parsed.ec==std::errc{}&&parsed.ptr==p.data()+p.size()&&port>0&&port<65535,Code::InvalidFormat,"Invalid original I2P DHT port");
  const auto& n=found.at("nid");require(n.size()==32,Code::InvalidFormat,"Invalid original I2P DHT NID length");
  dht::Id id{};unsigned acc=0,bits=0;std::size_t at=0;
  for(const auto c:n){const auto digit=std::string_view("abcdefghijklmnopqrstuvwxyz234567").find(c);require(digit!=std::string_view::npos,Code::InvalidFormat,"Invalid original I2P DHT NID encoding");
    acc=(acc<<5)|static_cast<unsigned>(digit);bits+=5;if(bits>=8){bits-=8;id[at++]=static_cast<Byte>((acc>>bits)&255U);}}
  require(at==id.size()&&!bits&&dht::validContact({id,hash,static_cast<std::uint16_t>(port),{}}),Code::Conflict,"Original DHT NID does not match its key/port");
  return {static_cast<std::uint16_t>(port),id};
}
}
std::expected<BootstrapResult,storage::Error> bootstrapI2pProfile(const storage::ProfileLease& lease,BootstrapOptions options,
    std::stop_token stop,Clock::time_point end,BootstrapObserver observer){
  try {
    require(options.mode==BootstrapMode::Normal,Code::Unsupported,"Preview never creates I2P identity files");
    require(options.key==BootstrapKey::Mixed||options.key==BootstrapKey::Pure,Code::Unsupported,"Unsupported original I2P key index");
    auto access=take(lease.sourceAccess());auto retained=take(lease.retain());budget(stop,end);
    const auto root = retained->directoryHandle();
    const auto rootStamp = take(io::stateStamp(root));
    require(rootStamp.directory && rootStamp.owned, Code::UnsafePath, "Owned normal profile required");
    PrivateMetadataProfile metadata(*retained);
    std::map<std::string,File> rootFiles;
    for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){rootFiles.emplace(name,capture(root,name,&metadata));}
    require(rootFiles.at("biglybt.config").stamp&&!rootFiles.at("biglybt.config.saving").stamp,Code::Conflict,"Unique primary I2P configuration required");
    auto config=take(storage::ConfigFile::load(lease,"biglybt.config"));
    require(config.recoverySource()==storage::RecoverySource::Primary&&config.document().root().type==core::Node::Type::Dictionary
        &&config.document().originalBytes()==read(rootFiles.at("biglybt.config"),storage::compatibleConfigLimits().inputBytes),Code::Conflict,"I2P primary configuration changed");
    auto binding=storage::originalPluginBinding("azneti2phelper",options.pluginDirectory);
    require(binding&&options.pluginDirectory!="."&&options.pluginDirectory!=".."&&options.pluginDirectory.find_first_of("/\\\0",0,3)==std::string::npos,Code::InvalidFormat,"Invalid original I2P plugin directory");
    storage::OriginalPluginSettings saved(config.document(),std::move(*binding));
    const bool startup=saved.loadedAtStartup(),enabled=saved.boolean("enable",true);
    const bool selected=saved.boolean(options.key==BootstrapKey::Pure?"azi2phelper.pure.enabled":"azi2phelper.mix.enabled",true);
    const bool discovery=saved.boolean("azi2phelper.dht.enable",true);
    require(startup&&enabled&&selected&&saved.wrongTypes().empty(),Code::Unsupported,"Original I2P plugin/index is disabled or malformed");
    const auto rootsCurrent=[&]{
      metadata.recheck();
      auto currentLease=take(lease.retain());require(currentLease->sharesOwnership(*retained),Code::Conflict,"Original I2P profile ownership changed");
      require(io::sameStateDirectory(rootStamp, take(io::stateStamp(root))), Code::Conflict, "Original I2P profile directory changed");
      for(const auto& [name,file]:rootFiles){current(root,name,file);}
    };
    rootsCurrent();
    // Empty owned directories are harmless after cancellation/crash. No key is
    // generated or published before original policy validation above.
    if(absent(root,"plugins")){
      access=take(lease.sourceAccess(true));budget(stop,end);rootsCurrent();
      createDirectory(root, "plugins");
    }
    auto plugins = directory(root, "plugins");
    const auto pluginsStamp = take(io::stateStamp(plugins.get()));
    if(absent(plugins.get(),options.pluginDirectory)){
      access=take(lease.sourceAccess(true));budget(stop,end);rootsCurrent();
      createDirectory(plugins.get(), options.pluginDirectory);
    }
    auto plugin = directory(plugins.get(), options.pluginDirectory);
    const auto pluginStamp = take(io::stateStamp(plugin.get()));
    const std::string suffix=options.key==BootstrapKey::Pure?"1":"";
    const std::string keyName="dest_key"+suffix+".dat",configName="dht"+suffix+".config";
    const std::string marker=".BiglyBTC.i2p-bootstrap"+suffix,keyTemp=".BiglyBTC."+keyName+".pending",configTemp=".BiglyBTC."+configName+".pending";
    std::map<std::string,File> files;
    for(const auto& name:{keyName,configName}){
      files.emplace(name,capture(plugin.get(),name,name==configName?&metadata:nullptr));
      for(const auto* extra:{".bak",".saving",".new",".bak.saving"}){const auto companion=name+extra;auto input=capture(plugin.get(),companion);require(!input.stamp,Code::Conflict,"Original I2P companion requires preserved review");files.emplace(companion,std::move(input));}
    }
    for(const auto& name:{marker,keyTemp,configTemp}){auto input=capture(plugin.get(),name);require(!input.stamp,Code::Conflict,"Partial native I2P bootstrap requires preserved review");files.emplace(name,std::move(input));}
    const auto allCurrent=[&]{
      rootsCurrent();
      require(namedDirectory(root, "plugins", plugins.get(), pluginsStamp)
          && namedDirectory(plugins.get(), options.pluginDirectory, plugin.get(), pluginStamp),
          Code::Conflict, "Named I2P plugin directories changed");
      for(const auto& [name,file]:files){current(plugin.get(),name,file);}
    };
    allCurrent();ClientIdentity identity;
    const bool newKey=!files.at(keyName).stamp,newConfig=!files.at(configName).stamp&&discovery;
    require(!newKey||!files.at(configName).stamp,Code::Conflict,"Existing DHT configuration without its key requires review");
    if(!newKey){
      require(io::stateAccessRestricted(files.at(keyName).fd.get(), false), Code::UnsafePath, "Original I2P key must be private");
#ifndef _WIN32
      require((files.at(keyName).stamp->mode & 07777) == 0600, Code::UnsafePath, "Original I2P private key must retain mode0600");
#endif
      const auto& file=files.at(keyName);require(file.stamp->size>=0&&static_cast<std::uint64_t>(file.stamp->size)<=MaximumClientIdentityInputBytes,Code::InvalidFormat,"Original I2P private key exceeds its bound");
      Secret input;const auto size=static_cast<std::size_t>(file.stamp->size);std::size_t offset=0;
      while(offset<size){const auto n=io::readStateAt(file.fd.get(),reinterpret_cast<char*>(input.bytes.data()+offset),size-offset,offset);
        require(n>0,Code::Io,"Cannot read held original private key");offset+=static_cast<std::size_t>(n);}
      require(sameFile(take(io::stateStamp(file.fd.get())), *file.stamp) && io::stateAccessRestricted(file.fd.get(), false),
          Code::Conflict, "Original private key changed while reading");
      auto decoded=decodeClientIdentity(View(input.bytes).first(size));
      require(bool(decoded),Code::InvalidFormat,"Unsupported or malformed existing I2P private key; preserved unchanged");identity=std::move(*decoded);
    }else{
      auto generated=generateClientIdentity();require(bool(generated),Code::Io,"Cannot generate original I2P private identity");identity=std::move(*generated);
    }
    BootstrapResult result{*identity.destination(),{},0,newKey,newConfig,discovery};std::string propertiesBytes;
    if(files.at(configName).stamp){const auto existing=properties(read(files.at(configName),64*1024),result.destination.hash);result.queryPort=existing.first;result.nodeId=existing.second;}
    else if(discovery){
      // Unbiased original nextInt(55533): port10000..65532; query+1 is valid.
      std::uint16_t random=0;do{budget(stop,end);require(RAND_bytes(reinterpret_cast<Byte*>(&random),sizeof(random))==1,Code::Io,"Cannot generate original I2P query port");}while(random>=55533);
      result.queryPort=static_cast<std::uint16_t>(10000+random);std::array<Byte,14> tail{};
      require(RAND_bytes(tail.data(),static_cast<int>(tail.size()))==1,Code::Io,"Cannot generate original I2P secure NID");
      auto id=dht::makeNodeId(result.destination.hash,result.queryPort,tail);require(bool(id),Code::InvalidFormat,"Cannot bind original I2P secure NID");result.nodeId=*id;
      propertiesBytes="# Original-compatible I2P DHT identity\nnid="+base32(*id)+"\nport="+std::to_string(result.queryPort)+"\n";
    }
    allCurrent();budget(stop,end);if(!newKey&&!newConfig){return result;}
    // Fixed private review marker, not a secret store or replay instruction.
    // Files are excluded from all permission/grant decisions by this API.
    const std::string markerBytes="BIGLYBTC_I2P_BOOTSTRAP_REVIEW_V1\nindex="+(suffix.empty()?std::string("0"):suffix)+"\n";
    access=take(lease.sourceAccess(true));allCurrent();budget(stop,end);
    const auto write=[&](const std::string& name,std::string_view bytes){
      allCurrent();budget(stop,end);require(!files.at(name).stamp,Code::Conflict,"I2P first publication cannot replace existing data");
      auto fd = take(io::stageStateFile(plugin.get(), name, bytes, 0600));
      take(io::protectPrivateStateFile(fd.get()));
      take(io::syncStateFile(fd.get()));
      const auto actual = take(io::stateStamp(fd.get()));
      const auto named = take(io::stateStampAt(plugin.get(), name));
      require(named && sameFile(actual, *named) && actual.size >= 0
          && static_cast<std::uint64_t>(actual.size) == bytes.size(),
          Code::Conflict, "Owned I2P temporary identity changed; preserved review required");
      take(io::syncStateDirectory(plugin.get()));
      files.at(name) = File{std::move(fd), actual};
    };
    const auto observe=[&](BootstrapStage stage){if(observer){access.outside([&]{observer(stage);});allCurrent();}};
    const auto publish=[&](const std::string& from,const std::string& to){
      allCurrent();budget(stop,end);auto& temporary=files.at(from);auto& target=files.at(to);
      require(temporary.stamp&&!target.stamp,Code::Conflict,"I2P exclusive publication inputs changed");const auto before=*temporary.stamp;
      take(io::publishStateFile(plugin.get(), from, to, temporary.fd.get(), false, &before));
      const auto actual = take(io::stateStamp(temporary.fd.get()));
      const auto named = take(io::stateStampAt(plugin.get(), to));
      require(named && sameFile(actual, *named) && io::sameStateIdentity(actual, before)
          && actual.mode == before.mode && actual.owner == before.owner && actual.group == before.group
          && actual.size == before.size && io::sameStateTimestamp(actual.modified, before.modified)
          && io::stateAccessRestricted(temporary.fd.get(), false),
          Code::Conflict, "I2P first publication identity is uncertain; preserve review");
      take(io::syncStateDirectory(plugin.get()));
      target = File{std::move(temporary.fd), actual};
      temporary = File{};
    };
    write(marker,markerBytes);observe(BootstrapStage::JournalSynced);
    if(newKey){const auto bytes=identity.keyStream();write(keyTemp,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});observe(BootstrapStage::KeyTemporarySynced);publish(keyTemp,keyName);observe(BootstrapStage::KeyPublished);}
    if(newConfig){write(configTemp,propertiesBytes);observe(BootstrapStage::ConfigTemporarySynced);publish(configTemp,configName);observe(BootstrapStage::ConfigPublished);}
    allCurrent();
    auto& review = files.at(marker);
    review.fd = Descriptor{};
    take(io::retireStateFile(plugin.get(), marker, *review.stamp));
    take(io::syncStateDirectory(plugin.get()));
    review = File{};
    observe(BootstrapStage::Completed);return result;
  }catch(const Failure& f){return std::unexpected(f.error);}
  catch(const std::bad_alloc&){return std::unexpected(storage::Error{Code::Io,"Insufficient memory; preserve any pending I2P bootstrap files"});}
  catch(...){return std::unexpected(storage::Error{Code::Conflict,"I2P bootstrap interrupted; preserve pending files for review"});}
}
}
