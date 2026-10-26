#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <optional>
#include <limits>
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
