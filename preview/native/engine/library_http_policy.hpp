#pragma once
#include "library_http_tracker.hpp"
#include "library_udp_policy.hpp"
#include "library_http_trust.hpp"
#include <QCryptographicHash>
#include <algorithm>
namespace biglybtc::engine::detail {
struct LibraryHttpConfiguration {std::string scope;bool singleOnly=false;std::optional<QList<QSslCertificate>> trustAnchors=std::nullopt;};
inline std::expected<LibraryHttpConfiguration,storage::Error> libraryHttpConfiguration(const storage::ProfileLease& lease,bool https=false,
    std::optional<storage::ConfigFile>* authenticationSnapshot=nullptr,const NetworkPolicy* routing=nullptr){
  const auto unsupported=[](){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Standalone HTTP scrape requires enabled TCP scrape settings and default tracker overrides"});};
  auto settings=udpTrackerConfig(lease,"biglybt.config");if(!settings){return std::unexpected(settings.error());}
  bool single=false;std::uint32_t probe=1,udp=1;std::string authScope="absent",internalScope="absent";
  if(*settings){const auto& root=(**settings).document().root();if(!udpUnique(root)){return unsupported();}
    const auto number=[&](const char* name,std::uint32_t fallback)->std::optional<std::uint32_t>{const auto* value=root.find(name);if(!value){return fallback;}if(value->type!=core::Node::Type::Integer){return {};}return static_cast<std::uint32_t>(value->integer);};
    const auto enabled=number("Tracker Client Scrape Enable",1),disabled=number("Tracker Client Scrape Total Disable",0),tcp=number("Tracker Client Enable TCP",1),one=number("Tracker Client Scrape Single Only",0),p=number("Tracker UDP Probe Enable",1),u=number("Server Enable UDP",1);
    if(!enabled||!*enabled||!disabled||*disabled||!tcp||!*tcp||!one||!p||!u){return unsupported();}single=*one!=0;probe=*p;udp=*u;
    // This original branch chooses <internal> credentials ahead of the saved
    // cache. Bind its exact absence/type/value to every approval and delivery.
    if(const auto* self=root.find("Tracker IP")){
      const auto raw=core::encodeValue(*self,storage::compatibleConfigLimits());if(!raw||raw->size()>2048){return unsupported();}
      internalScope=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha256).toHex().toStdString();
    }
    if(const auto* cache=root.find("swt.auth.persistent.cache")){
      if(!udpUnique(*cache)||cache->children.size()>256){return unsupported();}
      const auto raw=core::encodeValue(*cache,storage::compatibleConfigLimits());if(!raw||raw->size()>128*1024){return unsupported();}
      authScope=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha256).toHex().toStdString();
    }
  }
  auto trackers=udpTrackerConfig(lease,"alltrackers.config");if(!trackers){return std::unexpected(trackers.error());}
  if(*trackers){const auto& root=(**trackers).document().root();if(!udpUnique(root)){return unsupported();}
    if(const auto* rows=root.find("trackers")){if(rows->type!=core::Node::Type::List||rows->children.size()>4096){return unsupported();}
      for(const auto& row:rows->children){if(!udpUnique(row)){return unsupported();}if(const auto* options=row.find("op")){if(!udpUnique(*options)){return unsupported();}
        if(const auto* level=options->find("sl");level&&(level->type!=core::Node::Type::Integer||static_cast<std::uint32_t>(level->integer)!=0)){return unsupported();}
      }}
    }
  }
  // A live owner's selected proxy may not silently survive a saved route or
  // credential change. Exact source bytes also revoke an already queued hop.
  if(routing){
    const auto current=readNetworkPolicy(*settings?&(**settings).document():nullptr,
        {.mode=NetworkMode::Normal,.socks4Capability=routing->socks4Capability});
    if(!current){return unsupported();}const auto& a=current->proxy;const auto& b=routing->proxy;
    if(a.kind!=b.kind||a.host!=b.host||a.port!=b.port||a.username!=b.username||a.password!=b.password
        ||a.peers!=b.peers||a.trackers!=b.trackers||a.remoteDns!=b.remoteDns){return unsupported();}
  }
  QByteArray proxySource;
  for(const auto key:{"Enable.Proxy","Enable.SOCKS","Proxy.Host","Proxy.Port","Proxy.Username","Proxy.Password","Proxy.SOCKS.Tracker.DNS.Disable",
      "Proxy.Data.Enable","Proxy.Data.Same","Proxy.Data.SOCKS.version","Proxy.Data.Host","Proxy.Data.Port","Proxy.Data.Username","Proxy.Data.Password"}){
    proxySource+=key;proxySource+=':';
    const auto* value=*settings?(**settings).document().find(key):nullptr;
    if(value){const auto bytes=core::encodeValue(*value,storage::compatibleConfigLimits());if(!bytes||bytes->size()>4096){return unsupported();}proxySource+=QByteArray::fromStdString(*bytes);}else{proxySource+="absent";}
    proxySource+=';';
  }
  LibraryHttpConfiguration result{"original-http-single-request:tcp:single="+std::to_string(single)+":udp-probe="+std::to_string(probe)+":udp="+std::to_string(udp)+":auth="+authScope+":internal="+internalScope+":proxy="+QCryptographicHash::hash(proxySource,QCryptographicHash::Sha256).toHex().toStdString(),single};
  if(https){auto trust=libraryHttpTrust(lease);if(!trust){return std::unexpected(trust.error());}result.scope+=":"+trust->scope;result.trustAnchors=std::move(trust->anchors);}
  if(authenticationSnapshot){*authenticationSnapshot=std::move(*settings);}
  return result;
}
inline std::expected<std::optional<QByteArray>,storage::Error> libraryHttpSavedBasic(
    const core::Document* config,const LibraryHttpRoute& route,const QByteArray& realm){
  const auto unsupported=[](){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Saved Basic tracker credentials are outside the supported exact realm/origin ASCII scope"});};
  if(!config){return std::optional<QByteArray>{};}if(!udpUnique(config->root())){return unsupported();}const auto* cache=config->find("swt.auth.persistent.cache");
  if(!cache){return std::optional<QByteArray>{};}if(!udpUnique(*cache)||cache->children.size()>256||realm.size()>128){return unsupported();}
  const auto host=route.scrape.host();QHostAddress numeric(host);
  // Original AuthenticatorWindow has a separate <internal>/Tracker Password
  // branch for local addresses. This first saved-cache branch is hostname-only.
  if(!numeric.isNull()){return unsupported();}
  if(const auto* self=config->find("Tracker IP");self&&(self->type!=core::Node::Type::Bytes||self->bytes==host.toStdString())){return unsupported();}
  const auto key=realm.toStdString()+":"+route.scrape.scheme().toStdString()+"://"+host.toStdString()+":"+std::to_string(route.scrape.port(route.scrape.scheme()=="https"?443:80))+"/";
  const auto* row=cache->find(key);if(!row){return std::optional<QByteArray>{};}if(!udpUnique(*row)){return unsupported();}
  const auto* user=row->find("user");const auto* password=row->find("pw");
  const auto ascii=[](const core::Node* value){return value&&value->type==core::Node::Type::Bytes&&value->bytes.size()<=512
      &&std::ranges::all_of(value->bytes,[](unsigned char c){return c>=32&&c<=126;});};
  if(!ascii(user)||!ascii(password)||user->bytes.find(':')!=std::string::npos){return unsupported();}
  QByteArray raw(user->bytes.data(),static_cast<qsizetype>(user->bytes.size()));
  raw+=':';raw.append(password->bytes.data(),static_cast<qsizetype>(password->bytes.size()));
  return std::optional<QByteArray>{"Basic "+raw.toBase64()};
}
}
