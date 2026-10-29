#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
namespace biglybtc::core {
namespace udp_tracker_wire {
inline std::uint32_t word(std::string_view p,std::size_t at){
  std::uint32_t value=0;for(unsigned i=0;i<4;++i){value=(value<<8)|static_cast<unsigned char>(p[at+i]);}return value;
}
inline void put(std::span<char> out,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i){out[at+i]=static_cast<char>(value>>(24-8*i));}}
inline std::array<char,16> connect(std::uint32_t transaction){
  std::array<char,16> p{};put(p,0,0x417);put(p,4,0x27101980);put(p,8,0);put(p,12,transaction);return p;
}
inline std::array<char,36> scrape(std::uint64_t connection,std::uint32_t transaction,std::span<const std::byte,20> hash){
  std::array<char,36> p{};put(p,0,static_cast<std::uint32_t>(connection>>32));put(p,4,static_cast<std::uint32_t>(connection));put(p,8,2);put(p,12,transaction);
  for(unsigned i=0;i<20;++i){p[16+i]=static_cast<char>(hash[i]);}return p;
}
enum class ReplyKind {Connect,Scrape,Error,Unsupported};
struct Reply {ReplyKind kind;std::uint64_t connection=0;std::int32_t seeds=0,downloaded=0,peers=0;};
// Canonical single-hash BEP15/VERSION2 subset. Wrong transaction/action,
// partial/multiple entries and trailing bytes are never accepted as counts.
inline std::optional<Reply> reply(std::string_view packet,std::uint32_t transaction,bool connecting){
  if(packet.size()<8||word(packet,4)!=transaction){return {};}
  if(packet.size()>1024){return Reply{ReplyKind::Unsupported};} // A truncated correlated reply cannot become timeout/error authority.
  const auto action=word(packet,0);if(action==3){return Reply{ReplyKind::Error};}
  if(connecting&&action==0&&packet.size()==16){return Reply{ReplyKind::Connect,(static_cast<std::uint64_t>(word(packet,8))<<32)|word(packet,12)};}
  if(!connecting&&action==2&&packet.size()==20){return Reply{ReplyKind::Scrape,0,std::bit_cast<std::int32_t>(word(packet,8)),std::bit_cast<std::int32_t>(word(packet,12)),std::bit_cast<std::int32_t>(word(packet,16))};}
  if((connecting&&action==0)||(!connecting&&action==2)){return Reply{ReplyKind::Unsupported};}
  return {};
}
} // namespace udp_tracker_wire
} // namespace biglybtc::core
