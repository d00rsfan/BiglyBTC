#pragma once
#include "library_udp_tracker.hpp"
#include <set>
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
namespace biglybtc::engine::detail {
inline std::expected<std::optional<storage::ConfigFile>,storage::Error> udpTrackerConfig(const storage::ProfileLease& lease,const char* name){
  struct stat pending{};const auto saving=std::string(name)+".saving";
  if(::fstatat(lease.directoryHandle(),saving.c_str(),&pending,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Pending standalone tracker settings require recovery"});}
  auto result=storage::ConfigFile::load(lease,name,{1024*1024,1024*1024,65536,64});
  if(!result){
    if(result.error().code!=storage::ErrorCode::Missing){return std::unexpected(result.error());}
    for(const auto suffix:{"",".bak",".saving"}){struct stat named{};const auto path=std::string(name)+suffix;
      if(::fstatat(lease.directoryHandle(),path.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Standalone tracker settings require complete primary or absent controls"});}
    }return std::optional<storage::ConfigFile>{};
  }
  if(result->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Recover tracker settings before standalone scrape"});}
  return std::optional<storage::ConfigFile>{std::move(*result)};
}
inline bool udpUnique(const core::Node& root){
  if(root.type!=core::Node::Type::Dictionary||root.children.size()%2){return false;}std::set<std::string_view> keys;
  for(std::size_t i=0;i<root.children.size();i+=2){if(root.children[i].type!=core::Node::Type::Bytes||!keys.insert(root.children[i].bytes).second){return false;}}return true;
}
// Actual original VERSION2 and enabled default scheduler subset. Nondefault
// per-tracker overrides stay explicitly unavailable rather than guessed from
// the saved tracker-count cache. Unknown configuration fields remain untouched.
inline std::expected<std::string,storage::Error> libraryUdpConfiguration(const storage::ProfileLease& lease,const NetworkPolicy* routing=nullptr){
  const auto unsupported=[](){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Standalone UDP scrape requires enabled original VERSION2 defaults without tracker overrides"});};
  auto config=udpTrackerConfig(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
  if(*config){const auto& root=(**config).document().root();if(!udpUnique(root)){return unsupported();}
    const auto number=[&](const char* name,std::uint32_t fallback)->std::optional<std::uint32_t>{const auto* value=root.find(name);if(!value){return fallback;}if(value->type!=core::Node::Type::Integer){return {};}return static_cast<std::uint32_t>(value->integer);};
    const auto version=number("Tracker Port UDP Version",2),enabled=number("Tracker Client Scrape Enable",1),disabled=number("Tracker Client Scrape Total Disable",0),udp=number("Server Enable UDP",1);
    if(!version||*version!=2||!enabled||!*enabled||!disabled||*disabled||!udp||!*udp){return unsupported();}
  }
  auto trackers=udpTrackerConfig(lease,"alltrackers.config");if(!trackers){return std::unexpected(trackers.error());}
  if(*trackers){const auto& root=(**trackers).document().root();if(!udpUnique(root)){return unsupported();}
    if(const auto* rows=root.find("trackers")){if(rows->type!=core::Node::Type::List||rows->children.size()>4096){return unsupported();}
      for(const auto& row:rows->children){if(!udpUnique(row)){return unsupported();}if(const auto* options=row.find("op")){
        if(!udpUnique(*options)){return unsupported();}if(const auto* level=options->find("sl")){if(level->type!=core::Node::Type::Integer||static_cast<std::uint32_t>(level->integer)!=0){return unsupported();}}
      }}
    }
  }
  std::string source;
  if(routing){
    const auto current=readNetworkPolicy(*config?&(**config).document():nullptr,{.mode=NetworkMode::Normal,.socks4Capability=routing->socks4Capability});
    if(!current||!current->udpTrackers||current->proxy.kind!=NetworkProxyKind::None||routing->proxy.kind!=NetworkProxyKind::None){return unsupported();}
    // A selected DIRECT route cannot survive saved proxy/credential changes.
    // Snapshot exact absence/type/bytes, without retaining secrets in scope.
    for(const auto key:{"Enable.Proxy","Enable.SOCKS","Proxy.Host","Proxy.Port","Proxy.Username","Proxy.Password","Proxy.SOCKS.Tracker.DNS.Disable",
        "Proxy.Data.Enable","Proxy.Data.Same","Proxy.Data.SOCKS.version","Proxy.Data.Host","Proxy.Data.Port","Proxy.Data.Username","Proxy.Data.Password"}){
      source+=key;source+=':';const auto* value=*config?(**config).document().find(key):nullptr;
      if(value){const auto bytes=core::encodeValue(*value,storage::compatibleConfigLimits());if(!bytes||bytes->size()>4096){return unsupported();}source+=*bytes;}else{source+="absent";}source+=';';
    }
  }
  std::string result="original-udp-v2:enabled:retry1:stage30000:default-tracker-options";
  if(routing){std::array<unsigned char,32> digest{};unsigned size=0;
    if(EVP_Digest(source.data(),source.size(),digest.data(),&size,EVP_sha256(),nullptr)!=1||size!=digest.size()){return unsupported();}
    constexpr char hex[]="0123456789abcdef";result+=":proxy=";for(const auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}}
  return result;
}
inline std::string libraryUdpRouteScope(const LibraryUdpRoute& route,std::string_view config){
  return std::string(config)+":"+route.local.address().to_string()+":"+route.remote.address().to_string()+":"+std::to_string(route.remote.port());
}
} // namespace biglybtc::engine::detail
