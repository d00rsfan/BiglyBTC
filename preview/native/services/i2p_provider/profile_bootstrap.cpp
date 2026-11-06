// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_bootstrap.hpp"
#include "generate_identity.hpp"
#include "file_io.hpp"
#include "plugin_settings.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <map>
#include <new>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::services::i2p::provider {
namespace {
using storage::Error;using Code=storage::ErrorCode;using storage::detail::Descriptor;
using Clock=std::chrono::steady_clock;
struct Failure {Error error;};
void require(bool ok,Code code,const char* text){if(!ok){throw Failure{{code,text}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Failure{value.error()};}return std::move(*value);}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode
    &&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size
    &&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
    &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
bool sameDirectory(const struct stat& a,const struct stat& b){return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)
    &&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid;}
struct File {Descriptor fd{-1};std::optional<struct stat> stamp;};
File capture(int directory,const std::string& name){
  struct stat named{};
  if(::fstatat(directory,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT,Code::Io,"Cannot inspect I2P bootstrap input");return {};}
  require(S_ISREG(named.st_mode)&&named.st_nlink==1&&named.st_uid==::geteuid()
      &&!(named.st_mode&0022),Code::UnsafePath,"I2P bootstrap input must be owned regular single-link data");
  Descriptor fd(::openat(directory,name.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));struct stat held{};
  require(fd.get()>=0&&::fstat(fd.get(),&held)==0&&same(named,held),Code::Conflict,"I2P bootstrap input changed while opening");
  return {std::move(fd),held};
}
void current(int directory,const std::string& name,const File& file){
  struct stat named{},held{};const int rc=::fstatat(directory,name.c_str(),&named,AT_SYMLINK_NOFOLLOW);
  require(file.stamp?(rc==0&&::fstat(file.fd.get(),&held)==0&&same(held,*file.stamp)&&same(named,held)):(rc!=0&&errno==ENOENT),
      Code::Conflict,"I2P bootstrap input changed; preserve pending files for review");
}
std::string read(const File& file,std::size_t maximum){
  require(file.stamp&&file.stamp->st_size>=0&&static_cast<std::uint64_t>(file.stamp->st_size)<=maximum,Code::InvalidFormat,"I2P bootstrap input exceeds its bound");
  std::string bytes(static_cast<std::size_t>(file.stamp->st_size),'\0');std::size_t at=0;
  while(at<bytes.size()){const auto n=::pread(file.fd.get(),bytes.data()+at,bytes.size()-at,static_cast<off_t>(at));
    if(n<0&&errno==EINTR){continue;}require(n>0,Code::Io,"Cannot read held I2P bootstrap input");at+=static_cast<std::size_t>(n);}
  struct stat after{};require(::fstat(file.fd.get(),&after)==0&&same(after,*file.stamp),Code::Conflict,"I2P bootstrap input changed during read");return bytes;
}
struct Secret {
  std::array<Byte,MaximumClientIdentityInputBytes> bytes{};
  ~Secret(){OPENSSL_cleanse(bytes.data(),bytes.size());}
};
void budget(std::stop_token stop,Clock::time_point end){require(!stop.stop_requested()&&Clock::now()<end,Code::Cancelled,"I2P profile bootstrap cancelled before publication");}
Descriptor directory(int parent,const std::string& name){
  Descriptor result(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat st{};
  require(result.get()>=0&&::fstat(result.get(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==::geteuid()&&!(st.st_mode&0022),Code::UnsafePath,"Owned I2P plugin directory required");return result;
}
bool absent(int parent,const std::string& name){struct stat st{};if(::fstatat(parent,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0){return false;}require(errno==ENOENT,Code::Io,"Cannot inspect I2P plugin directory");return true;}
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
    const auto root=retained->directoryHandle();struct stat rootStamp{};
    require(::fstat(root,&rootStamp)==0&&S_ISDIR(rootStamp.st_mode)&&rootStamp.st_uid==::geteuid(),Code::UnsafePath,"Owned normal profile required");
    std::map<std::string,File> rootFiles;
    for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){rootFiles.emplace(name,capture(root,name));}
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
      auto currentLease=take(lease.retain());require(currentLease->sharesOwnership(*retained),Code::Conflict,"Original I2P profile ownership changed");
      struct stat now{};require(::fstat(root,&now)==0&&sameDirectory(rootStamp,now),Code::Conflict,"Original I2P profile directory changed");
      for(const auto& [name,file]:rootFiles){current(root,name,file);}
    };
    rootsCurrent();
    // Empty owned directories are harmless after cancellation/crash. No key is
    // generated or published before original policy validation above.
    if(absent(root,"plugins")){
      access=take(lease.sourceAccess(true));budget(stop,end);rootsCurrent();
      require(::mkdirat(root,"plugins",0700)==0&&::fsync(root)==0,Code::Io,"Cannot create owned plugin directory");
    }
    auto plugins=directory(root,"plugins");struct stat pluginsStamp{};require(::fstat(plugins.get(),&pluginsStamp)==0,Code::Io,"Cannot inspect held plugins directory");
    if(absent(plugins.get(),options.pluginDirectory)){
      access=take(lease.sourceAccess(true));budget(stop,end);rootsCurrent();
      require(::mkdirat(plugins.get(),options.pluginDirectory.c_str(),0700)==0&&::fsync(plugins.get())==0,Code::Io,"Cannot create owned I2P plugin directory");
    }
    auto plugin=directory(plugins.get(),options.pluginDirectory);struct stat pluginStamp{};require(::fstat(plugin.get(),&pluginStamp)==0,Code::Io,"Cannot inspect held I2P directory");
    const std::string suffix=options.key==BootstrapKey::Pure?"1":"";
    const std::string keyName="dest_key"+suffix+".dat",configName="dht"+suffix+".config";
    const std::string marker=".BiglyBTC.i2p-bootstrap"+suffix,keyTemp=".BiglyBTC."+keyName+".pending",configTemp=".BiglyBTC."+configName+".pending";
    std::map<std::string,File> files;
    for(const auto& name:{keyName,configName}){
      files.emplace(name,capture(plugin.get(),name));
      for(const auto* extra:{".bak",".saving",".new",".bak.saving"}){const auto companion=name+extra;auto input=capture(plugin.get(),companion);require(!input.stamp,Code::Conflict,"Original I2P companion requires preserved review");files.emplace(companion,std::move(input));}
    }
    for(const auto& name:{marker,keyTemp,configTemp}){auto input=capture(plugin.get(),name);require(!input.stamp,Code::Conflict,"Partial native I2P bootstrap requires preserved review");files.emplace(name,std::move(input));}
    const auto allCurrent=[&]{
      rootsCurrent();struct stat named{},held{};
      require(::fstat(plugins.get(),&held)==0&&sameDirectory(pluginsStamp,held)&&::fstatat(root,"plugins",&named,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(held,named)
          &&::fstat(plugin.get(),&held)==0&&sameDirectory(pluginStamp,held)&&::fstatat(plugins.get(),options.pluginDirectory.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(held,named),Code::Conflict,"Named I2P plugin directories changed");
      for(const auto& [name,file]:files){current(plugin.get(),name,file);}
    };
    allCurrent();ClientIdentity identity;
    const bool newKey=!files.at(keyName).stamp,newConfig=!files.at(configName).stamp&&discovery;
    require(!newKey||!files.at(configName).stamp,Code::Conflict,"Existing DHT configuration without its key requires review");
    if(!newKey){
      require((files.at(keyName).stamp->st_mode&07777)==0600,Code::UnsafePath,"Original I2P private key must retain mode0600");
      const auto& file=files.at(keyName);require(file.stamp->st_size>=0&&static_cast<std::uint64_t>(file.stamp->st_size)<=MaximumClientIdentityInputBytes,Code::InvalidFormat,"Original I2P private key exceeds its bound");
      Secret input;const auto size=static_cast<std::size_t>(file.stamp->st_size);std::size_t offset=0;
      while(offset<size){const auto n=::pread(file.fd.get(),input.bytes.data()+offset,size-offset,static_cast<off_t>(offset));
        if(n<0&&errno==EINTR){continue;}require(n>0,Code::Io,"Cannot read held original private key");offset+=static_cast<std::size_t>(n);}
      struct stat after{};require(::fstat(file.fd.get(),&after)==0&&same(after,*file.stamp),Code::Conflict,"Original private key changed while reading");
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
      auto fd=take(storage::detail::writeExclusiveOwned(plugin.get(),name,bytes,0600));struct stat actual{},named{};
      require(::fchmod(fd.get(),0600)==0&&::fsync(fd.get())==0&&::fstat(fd.get(),&actual)==0&&S_ISREG(actual.st_mode)
          &&actual.st_uid==::geteuid()&&actual.st_nlink==1&&(actual.st_mode&07777)==0600&&actual.st_size>=0
          &&static_cast<std::uint64_t>(actual.st_size)==bytes.size()&&::fstatat(plugin.get(),name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&same(actual,named)
          &&::fsync(plugin.get())==0,Code::Conflict,"Owned I2P temporary identity changed; preserved review required");
      files.at(name)=File{std::move(fd),actual};
    };
    const auto observe=[&](BootstrapStage stage){if(observer){access.outside([&]{observer(stage);});allCurrent();}};
    const auto publish=[&](const std::string& from,const std::string& to){
      allCurrent();budget(stop,end);auto& temporary=files.at(from);auto& target=files.at(to);
      require(temporary.stamp&&!target.stamp,Code::Conflict,"I2P exclusive publication inputs changed");const auto before=*temporary.stamp;
      require(::renameat2(plugin.get(),from.c_str(),plugin.get(),to.c_str(),RENAME_NOREPLACE)==0,Code::Conflict,"I2P first publication refused replacement; preserve pending files");
      struct stat actual{},named{};require(::fstat(temporary.fd.get(),&actual)==0&&::fstatat(plugin.get(),to.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&same(actual,named)
          &&actual.st_dev==before.st_dev&&actual.st_ino==before.st_ino&&actual.st_mode==before.st_mode&&actual.st_uid==before.st_uid&&actual.st_gid==before.st_gid
          &&actual.st_nlink==1&&actual.st_size==before.st_size&&actual.st_mtim.tv_sec==before.st_mtim.tv_sec&&actual.st_mtim.tv_nsec==before.st_mtim.tv_nsec
          &&::fsync(plugin.get())==0,Code::Conflict,"I2P first publication identity is uncertain; preserve review");
      target=File{std::move(temporary.fd),actual};temporary=File{};
    };
    write(marker,markerBytes);observe(BootstrapStage::JournalSynced);
    if(newKey){const auto bytes=identity.keyStream();write(keyTemp,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});observe(BootstrapStage::KeyTemporarySynced);publish(keyTemp,keyName);observe(BootstrapStage::KeyPublished);}
    if(newConfig){write(configTemp,propertiesBytes);observe(BootstrapStage::ConfigTemporarySynced);publish(configTemp,configName);observe(BootstrapStage::ConfigPublished);}
    allCurrent();require(::unlinkat(plugin.get(),marker.c_str(),0)==0&&::fsync(plugin.get())==0,Code::Io,"Completed I2P files retain uncertain bootstrap cleanup");files.at(marker)=File{};
    observe(BootstrapStage::Completed);return result;
  }catch(const Failure& f){return std::unexpected(f.error);}
  catch(const std::bad_alloc&){return std::unexpected(storage::Error{Code::Io,"Insufficient memory; preserve any pending I2P bootstrap files"});}
  catch(...){return std::unexpected(storage::Error{Code::Conflict,"I2P bootstrap interrupted; preserve pending files for review"});}
}
}
