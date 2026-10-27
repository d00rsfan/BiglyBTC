#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <optional>
#include <limits>
#include <string_view>
#include <string>
#include <vector>
namespace biglybtc::core {
// Original downloads.config maxdl/maxul byte rates. Zero is unlimited; negative
// original disabled values are deliberately outside this pre-add control.
inline constexpr std::int64_t MaximumTorrentIntakeRate=std::numeric_limits<std::int32_t>::max();
struct TorrentIntakeLimits {
  std::int64_t downloadBytesPerSecond=0,uploadBytesPerSecond=0;
  bool operator==(const TorrentIntakeLimits&)const=default;
};
inline constexpr bool validTorrentIntakeLimits(const TorrentIntakeLimits& value) noexcept {
  return value.downloadBytesPerSecond>=0&&value.uploadBytesPerSecond>=0
      &&value.downloadBytesPerSecond<=MaximumTorrentIntakeRate&&value.uploadBytesPerSecond<=MaximumTorrentIntakeRate;
}
// One explicit UTF8 destination leaf; no metadata name/path rewriting. Keep
// the platform's existing255-byte component limit and reserved-path boundary.
inline constexpr std::size_t MaximumTorrentIntakeSaveNameBytes=255;
inline bool validTorrentIntakeSaveName(std::string_view value) noexcept {
  if(value.empty()||value.size()>MaximumTorrentIntakeSaveNameBytes||value=="."||value==".."||value==".git"||value.starts_with(".BiglyBTC")){return false;}
  bool visible=false;
  for(std::size_t n=0;n<value.size();){
    const auto first=static_cast<unsigned char>(value[n++]);
    if(first<0x80){if(first<0x20||first==0x7f||first=='/'||first=='\\'){return false;}visible|=first!=' ';continue;}
    visible=true;std::uint32_t code=0,minimum=0;unsigned extra=0;
    if(first>=0xc2&&first<=0xdf){code=first&31;minimum=0x80;extra=1;}
    else if(first>=0xe0&&first<=0xef){code=first&15;minimum=0x800;extra=2;}
    else if(first>=0xf0&&first<=0xf4){code=first&7;minimum=0x10000;extra=3;}else{return false;}
    for(unsigned k=0;k<extra;++k){if(n==value.size()){return false;}const auto next=static_cast<unsigned char>(value[n++]);if((next&0xc0)!=0x80){return false;}code=(code<<6)|(next&63);}
    if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff)){return false;}
  }
  return visible;
}
// Explicit original-index destinations. Missing override inherits source
// initial_linkage; present empty clears it. The final filename is converted by
// the held original filename policy; an explicit parent is never rewritten.
struct TorrentIntakeFileDestination {
  std::uint32_t index=0;
  std::string path;
  bool operator==(const TorrentIntakeFileDestination&)const=default;
};
using TorrentIntakeFileDestinations=std::vector<TorrentIntakeFileDestination>;
inline bool validTorrentIntakeFilePath(std::string_view value) noexcept {
  if(value.size()<2||value.size()>4096||value.front()!='/'||value.back()=='/'){return false;}
  std::size_t begin=1;
  while(begin<value.size()){
    const auto end=value.find('/',begin);
    if(!validTorrentIntakeSaveName(value.substr(begin,end==std::string_view::npos?value.size()-begin:end-begin))){return false;}
    if(end==std::string_view::npos){return true;}begin=end+1;
  }
  return false;
}
inline bool validTorrentIntakeFileDestinations(const TorrentIntakeFileDestinations& values) noexcept {
  if(values.size()>4096){return false;}std::size_t bytes=0;std::uint32_t previous=0;bool first=true;
  for(const auto& value:values){
    if(value.index>=100000||(!first&&value.index<=previous)||!validTorrentIntakeFilePath(value.path)||value.path.size()>4*1024*1024-bytes){return false;}
    bytes+=value.path.size();previous=value.index;first=false;
  }
  return true;
}
// DownloadManagerState.AT_FLAGS / PIF Download.FLAG_SEQUENTIAL_DOWNLOAD.
inline constexpr std::int64_t OriginalSequentialDownloadFlag=0x00002000;
// Absent flags have Java's zero default. Every signed long bit is retained;
// wrong types/duplicate flags refuse instead of treating malformed data as zero.
inline std::optional<std::int64_t> originalDownloadFlags(const Node* attributes) {
  if(!attributes){return 0;}
  if(attributes->type!=Node::Type::Dictionary||attributes->children.size()%2){return {};}
  std::int64_t result=0;bool found=false;
  for(std::size_t n=0;n<attributes->children.size();n+=2){
    const auto& key=attributes->children[n];const auto& value=attributes->children[n+1];
    if(key.type!=Node::Type::Bytes){return {};}
    if(key.bytes!="flags"){continue;}
    if(found||value.type!=Node::Type::Integer){return {};}found=true;result=value.integer;
  }
  return result;
}
} // namespace biglybtc::core
