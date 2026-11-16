// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/peer_set_cidrs.hpp>
#include <algorithm>
#include <charconv>
#include <new>
#include <string_view>
namespace biglybtc::core::tags::peer_sets {
namespace {
std::optional<std::uint32_t> address(std::string_view text)noexcept{
 std::uint32_t result=0;std::size_t at=0;
 for(unsigned index=0;index<4;++index){
  const auto end=index<3?text.find('.',at):text.size();
  if(end==std::string_view::npos||end==at||end-at>3){return {};}
  unsigned value=0;const auto parsed=std::from_chars(text.data()+at,text.data()+end,value);
  if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+end||value>255){return {};}
  result=(result<<8)|value;at=end+1;
 }return result;
}
std::expected<Range,Error> range(std::string_view text)noexcept{
 const auto slash=text.find('/');if(slash==std::string_view::npos){return std::unexpected(Error::Unsupported);}
 auto host=address(text.substr(0,slash));if(!host){return std::unexpected(Error::Unsupported);}
 auto suffix=text.substr(slash+1);if(!suffix.empty()&&suffix.front()=='+'){suffix.remove_prefix(1);}
 if(suffix.empty()){return std::unexpected(Error::Unsupported);}
 unsigned bits=0;const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),bits);
 if(parsed.ec!=std::errc{}||parsed.ptr!=suffix.data()+suffix.size()||bits>32){return std::unexpected(Error::Unsupported);}
 const std::uint32_t mask=bits?std::uint32_t{0xffffffffU}<<(32-bits):0;
 const auto first=*host&mask;return Range{first,first|~mask};
}
}
std::expected<Ipv4CidrSet,Error> Ipv4CidrSet::create(std::span<const std::string> tokens,bool inverse,Limits limits){
 try{
  const Limits maximum;limits.ranges=std::min(limits.ranges,maximum.ranges);limits.tokenBytes=std::min(limits.tokenBytes,maximum.tokenBytes);
  if(tokens.empty()){return std::unexpected(Error::Invalid);}
  if(!limits.ranges||!limits.tokenBytes||tokens.size()>limits.ranges){return std::unexpected(Error::Bounds);}
  std::vector<Range> values;values.reserve(tokens.size());
  for(const auto& token:tokens){if(token.size()>limits.tokenBytes){return std::unexpected(Error::Bounds);}auto parsed=range(token);if(!parsed){return std::unexpected(parsed.error());}values.push_back(*parsed);}
  return Ipv4CidrSet(std::move(values),inverse);
 }catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
runtime::Truth Ipv4CidrSet::match(std::optional<std::uint32_t> publicIpv4)const noexcept{
 if(!publicIpv4||!*publicIpv4){return runtime::Truth::Unknown;}
 const bool found=std::ranges::any_of(ranges_,[&](const auto& range){return *publicIpv4>=range.first&&*publicIpv4<=range.last;});
 return found!=inverse_?runtime::Truth::Yes:runtime::Truth::No;
}
}
