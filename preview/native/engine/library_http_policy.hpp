#pragma once
#include "library_http_tracker.hpp"
#include "library_udp_policy.hpp"
namespace biglybtc::engine::detail {
struct LibraryHttpConfiguration {std::string scope;bool singleOnly=false;};
inline std::expected<LibraryHttpConfiguration,storage::Error> libraryHttpConfiguration(const storage::ProfileLease& lease){
  const auto unsupported=[](){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Standalone HTTP scrape requires enabled TCP scrape settings and default tracker overrides"});};
  auto settings=udpTrackerConfig(lease,"biglybt.config");if(!settings){return std::unexpected(settings.error());}
  bool single=false;std::uint32_t probe=1,udp=1;
  if(*settings){const auto& root=(**settings).document().root();if(!udpUnique(root)){return unsupported();}
    const auto number=[&](const char* name,std::uint32_t fallback)->std::optional<std::uint32_t>{const auto* value=root.find(name);if(!value){return fallback;}if(value->type!=core::Node::Type::Integer){return {};}return static_cast<std::uint32_t>(value->integer);};
    const auto enabled=number("Tracker Client Scrape Enable",1),disabled=number("Tracker Client Scrape Total Disable",0),tcp=number("Tracker Client Enable TCP",1),one=number("Tracker Client Scrape Single Only",0),p=number("Tracker UDP Probe Enable",1),u=number("Server Enable UDP",1);
    if(!enabled||!*enabled||!disabled||*disabled||!tcp||!*tcp||!one||!p||!u){return unsupported();}single=*one!=0;probe=*p;udp=*u;
  }
  auto trackers=udpTrackerConfig(lease,"alltrackers.config");if(!trackers){return std::unexpected(trackers.error());}
  if(*trackers){const auto& root=(**trackers).document().root();if(!udpUnique(root)){return unsupported();}
    if(const auto* rows=root.find("trackers")){if(rows->type!=core::Node::Type::List||rows->children.size()>4096){return unsupported();}
      for(const auto& row:rows->children){if(!udpUnique(row)){return unsupported();}if(const auto* options=row.find("op")){if(!udpUnique(*options)){return unsupported();}
        if(const auto* level=options->find("sl");level&&(level->type!=core::Node::Type::Integer||static_cast<std::uint32_t>(level->integer)!=0)){return unsupported();}
      }}
    }
  }
  return LibraryHttpConfiguration{"original-http-single-request:tcp:single="+std::to_string(single)+":udp-probe="+std::to_string(probe)+":udp="+std::to_string(udp),single};
}
}
