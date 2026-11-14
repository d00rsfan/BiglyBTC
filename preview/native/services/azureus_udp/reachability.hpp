// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTUDPPacketHandler and DHTTransportUDPImpl/Average policy.
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>
namespace biglybtc::services::azureus_udp {
struct ReachabilityFacts {bool reachable=true,accurate=false;std::uint64_t aliens=0,findValueAliens=0;std::int64_t average=0,findValueAverage=0;};
// Memory-only observation. No peer admission, route, socket or timer authority.
class Reachability final {
 struct Bloom {
  std::array<std::uint8_t,1251> bits{}; // Original requested10000 -> odd10001.
  std::array<unsigned,5> indices(std::span<const std::uint8_t> ip)const{
   std::uint32_t v=0x51f7ac81u;for(auto b:ip){v=v*191u+b;}const std::array h{v,v*v,v*2u+51u,v*3u+145u,v*5u+216u};std::array<unsigned,5> out{};
   for(unsigned i=0;i<5;++i){const auto r=std::bit_cast<std::int32_t>(h[i])%10001;out[i]=unsigned(r<0?-r:r);}return out;
  }
  bool contains(std::span<const std::uint8_t> ip)const{for(auto n:indices(ip)){if(!(bits[n/8]&(1u<<(n%8)))){return false;}}return true;}
  void add(std::span<const std::uint8_t> ip){for(auto n:indices(ip)){bits[n/8]|=std::uint8_t(1u<<(n%8));}}
 };
 struct Average {
  std::array<std::int64_t,12> values{};std::int64_t last=0;
  explicit Average(std::int64_t now):last(now/60000){}
  void update(std::int64_t factor){if(last<factor-12){last=factor-13;}for(auto i=last+1;i<=factor;++i){values[std::size_t(i%12)]=0;}values[std::size_t((factor+1)%12)]=0;last=factor;}
  void add(std::int64_t now,std::uint64_t count){const auto factor=now/60000;update(factor);values[std::size_t(factor%12)]+=std::int64_t(count)*60;}
  std::int64_t get(std::int64_t now){const auto factor=now/60000;update(factor);std::int64_t sum=0;for(auto i=factor+2;i<factor+12;++i){sum+=values[std::size_t(i%12)];}return sum/600;}
 };
 Bloom first_,second_;Average all_,find_;ReachabilityFacts facts_;std::int64_t started_=0,lastRotation_=0,lastSample_=0;std::uint64_t lastAll_=0,lastFind_=0;
public:
 explicit Reachability(bool initial,std::int64_t now):all_(now),find_(now),started_(now),lastSample_(now){facts_.reachable=initial;}
 void sent(std::span<const std::uint8_t> ip,std::int64_t now){if(ip.size()!=4&&ip.size()!=16){return;}if(now<lastRotation_||now-lastRotation_>180000){first_=second_;second_={};lastRotation_=now;}first_.add(ip);second_.add(ip);}
 bool received(std::span<const std::uint8_t> ip,std::int32_t action){if(ip.size()!=4&&ip.size()!=16){return false;}if(first_.contains(ip)){return false;}first_.add(ip);second_.add(ip);++facts_.aliens;if(action==1030){++facts_.findValueAliens;}return true;}
 bool sample(std::int64_t now){if(now<0||now<lastSample_||now-lastSample_<60000){return false;}lastSample_=now;all_.add(now,facts_.aliens-lastAll_);find_.add(now,facts_.findValueAliens-lastFind_);lastAll_=facts_.aliens;lastFind_=facts_.findValueAliens;facts_.average=all_.get(now);facts_.findValueAverage=find_.get(now);const bool previous=facts_.reachable;if(now-started_>900000){facts_.accurate=true;facts_.reachable=facts_.findValueAverage>1||facts_.average>3;}return facts_.reachable!=previous;}
 ReachabilityFacts facts()const{return facts_;}
};
}
