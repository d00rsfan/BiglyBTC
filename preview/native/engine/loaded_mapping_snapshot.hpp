// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
namespace biglybtc::engine::detail {
struct LoadedMapping {
  std::uintptr_t begin=0,end=0;
  std::uint64_t offset=0,deviceMajor=0,deviceMinor=0,inode=0;
  bool operator==(const LoadedMapping&)const=default;
};
inline constexpr std::size_t MaximumMappedSymbols=47;
using LoadedMappings=std::array<LoadedMapping,MaximumMappedSymbols>;
namespace mapping_detail {
template<class Number> bool number(std::string_view token,Number& result,int base)noexcept {
  const auto parsed=std::from_chars(token.data(),token.data()+token.size(),result,base);
  return parsed.ec==std::errc{}&&parsed.ptr==token.data()+token.size();
}
inline std::optional<std::string_view> field(std::string_view& line)noexcept {
  const auto begin=line.find_first_not_of(' ');if(begin==std::string_view::npos){return {};}
  line.remove_prefix(begin);const auto end=line.find(' ');const auto result=line.substr(0,end);
  line.remove_prefix(end==std::string_view::npos?line.size():end);return result;
}
}
// Same line/byte/field/range/executable-file checks as the original individual
// lookup. Read the complete bounded stream once for ALL symbols, including
// unrelated lines: finding all addresses is not permission to skip bad input.
inline std::optional<LoadedMappings> readLoadedMappings(
    FILE* file,std::span<const std::uintptr_t> addresses)noexcept {
  if(!file||addresses.empty()||addresses.size()>MaximumMappedSymbols){return {};}
  LoadedMappings result{};std::array<bool,MaximumMappedSymbols> found{};
  const auto [minimum,maximum]=std::minmax_element(addresses.begin(),addresses.end());
  std::array<char,8192> buffer{};std::size_t bytes=0,lines=0;
  while(std::fgets(buffer.data(),int(buffer.size()),file)){
    const auto length=std::strlen(buffer.data());bytes+=length;
    if(++lines>16384||bytes>2*1024*1024||!length||buffer[length-1]!='\n'){return {};}
    std::string_view line(buffer.data(),length-1);
    const auto range=mapping_detail::field(line),permissions=mapping_detail::field(line),
        offset=mapping_detail::field(line),device=mapping_detail::field(line),inode=mapping_detail::field(line);
    if(!range||!permissions||!offset||!device||!inode){return {};}
    const auto dash=range->find('-'),colon=device->find(':');
    if(dash==std::string_view::npos||colon==std::string_view::npos){return {};}
    LoadedMapping candidate;
    if(!mapping_detail::number(range->substr(0,dash),candidate.begin,16)
        ||!mapping_detail::number(range->substr(dash+1),candidate.end,16)
        ||!mapping_detail::number(*offset,candidate.offset,16)
        ||!mapping_detail::number(device->substr(0,colon),candidate.deviceMajor,16)
        ||!mapping_detail::number(device->substr(colon+1),candidate.deviceMinor,16)
        ||!mapping_detail::number(*inode,candidate.inode,10)||candidate.begin>=candidate.end){return {};}
    if(candidate.end<=*minimum||candidate.begin>*maximum){continue;}
    for(std::size_t i=0;i<addresses.size();++i){
      if(addresses[i]<candidate.begin||addresses[i]>=candidate.end){continue;}
      if(found[i]||permissions->size()!=4||(*permissions)[0]!='r'||(*permissions)[2]!='x'||!candidate.inode){return {};}
      found[i]=true;result[i]=candidate;
    }
  }
  if(std::ferror(file)||!std::all_of(found.begin(),found.begin()+std::ptrdiff_t(addresses.size()),[](bool value){return value;})){return {};}
  return result;
}
}
