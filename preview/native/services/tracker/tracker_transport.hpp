// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::tracker {

enum class Transport { Http, Https, Udp };

inline std::optional<Transport> sharingTrackerTransport(std::string_view name) {
  const auto equals=[&](std::string_view lower) {
    return std::ranges::equal(name,lower,[](char a,char b) {
      return (a>='A'&&a<='Z'?a+('a'-'A'):a)==b;
    });
  };
  if(equals("http")){return Transport::Http;}
  if(equals("https")){return Transport::Https;}
  if(equals("udp")){return Transport::Udp;}
  return {};
}

inline std::string trackerAnnounce(Transport transport,std::string_view host,std::uint16_t port,bool udpAuthentication=false) {
  std::string url;
  switch(transport){
    case Transport::Http: url="http://";break;
    case Transport::Https: url="https://";break;
    case Transport::Udp: url="udp://";break;
  }
  if(host.contains(':')){url+='[';url+=host;url+=']';}
  else{url+=host;}
  url+=':';
  url+=std::to_string(port);
  url+="/announce";
  if(transport==Transport::Udp&&udpAuthentication){url+="?auth";}
  return url;
}

} // namespace biglybtc::tracker
