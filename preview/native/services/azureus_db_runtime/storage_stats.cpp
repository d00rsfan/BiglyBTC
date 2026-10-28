// SPDX-License-Identifier: GPL-2.0-or-later
#include "storage_stats.hpp"
#include <bit>
#include <algorithm>
namespace biglybtc::services::azureus_storage_stats {
namespace {
std::int32_t plus(std::int32_t a,std::int32_t b)noexcept{return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)+std::bit_cast<std::uint32_t>(b));}
void put(std::string& out,std::int32_t n){const auto raw=std::bit_cast<std::uint32_t>(n);for(int shift:{24,16,8,0}){out.push_back(static_cast<char>(raw>>shift));}}
std::int32_t get(std::string_view s,std::size_t at){std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|static_cast<unsigned char>(s[at+i]);}return std::bit_cast<std::int32_t>(n);}
}
std::string encode(const Stats& s){std::string out;out.reserve(14);out.push_back(0);put(out,s.entries);put(out,s.bytes);put(out,s.readsPerMinute);out.push_back(static_cast<char>(s.diversification));return out;}
std::expected<Decoded,Error> decode(std::string_view raw){
 if(raw.size()<14){return std::unexpected(Error::Invalid);}if(raw.size()>512){return std::unexpected(Error::Limit);}
 return Decoded{Stats{get(raw,1),get(raw,5),get(raw,9),std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(raw[13]))},static_cast<std::uint8_t>(raw[0]),std::string(raw.substr(14))};
}
void Key::changed(std::int32_t entries,std::int32_t bytes)noexcept{entries_=std::max(plus(entries_,entries),0);bytes_=std::max(plus(bytes_,bytes),0);}
std::expected<void,Error> Key::read(std::span<const std::uint8_t> ip,std::int64_t now)noexcept{
 if(ip.size()!=4&&ip.size()!=16){return std::unexpected(Error::Invalid);}
 const auto diff=std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(now)-std::bit_cast<std::uint64_t>(start_));
 if(diff>180000){
  if(active_){reads_=static_cast<std::int16_t>(bloomEntries_/3);if(reads_==0&&bloomEntries_>0){reads_=1;}}
  start_=now;active_=false; // Source drops this read, including an initially late read.
 }else{
  if(!active_){bloom_.fill(0);bloomEntries_=0;active_=true;}
  std::uint32_t raw=0x51f7ac81U;for(auto b:ip){raw=raw*191U+b;}
  const std::array<std::uint32_t,5> hashes{raw,raw*raw,raw*2U+51U,raw*3U+145U,raw*5U+216U};bool fresh=false;
  for(auto h:hashes){auto index=std::bit_cast<std::int32_t>(h)%301;if(index<0){index=-index;}auto& cell=bloom_[static_cast<std::size_t>(index)/8];const auto mask=static_cast<std::uint8_t>(1U<<(index%8));if(!(cell&mask)){fresh=true;cell|=mask;}}
  if(fresh){++bloomEntries_;}
 }return {};
}
}
