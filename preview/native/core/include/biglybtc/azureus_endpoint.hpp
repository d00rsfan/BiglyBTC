// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
namespace biglybtc::core {
// Numeric unicast-only initial PublicBound subset. Original UDP validity also
// excludes loopback/link/site-local; this native scope additionally refuses
// unspecified, multicast, mapped aliases, IPv6 ULA and shared IPv4 space.
// Documentation prefixes are not proof of Internet reachability.
inline bool azureusPublicAddress(std::span<const std::uint8_t> bytes)noexcept{
  if(bytes.size()==4){const auto a=bytes[0],b=bytes[1];return a!=0&&a!=10&&a!=127&&a<224&&!(a==169&&b==254)&&!(a==172&&b>=16&&b<=31)&&!(a==192&&b==168)&&!(a==100&&b>=64&&b<=127);}
  if(bytes.size()!=16||bytes[0]==255||(bytes[0]&0xfe)==0xfc||(bytes[0]==0xfe&&(bytes[1]&0xc0)>=0x80)){return false;}
  bool prefixZero=true;for(unsigned i=0;i<15;++i){prefixZero&=bytes[i]==0;}if(prefixZero&&(bytes[15]==0||bytes[15]==1)){return false;}
  bool mapped=true;for(unsigned i=0;i<10;++i){mapped&=bytes[i]==0;}return !(mapped&&bytes[10]==255&&bytes[11]==255);
}
}
