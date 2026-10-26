#pragma once
#include "biglybtc/bencode.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::core {
using TorrentIntakeTrackerTiers=std::vector<std::vector<std::string>>;
inline constexpr std::size_t MaximumIntakeTrackerTiers=16,MaximumIntakeTrackers=64,
    MaximumIntakeTrackerUrlBytes=2048,MaximumIntakeTrackerBytes=65536;
struct TorrentIntakeTrackerFields {
  std::string announce;
  TorrentIntakeTrackerTiers tiers; // Exact stored announce-list; empty means no sets.
  bool operator==(const TorrentIntakeTrackerFields&)const=default;
};
inline bool boundedTorrentIntakeTrackers(const TorrentIntakeTrackerFields& fields) noexcept {
  if(fields.announce.size()>MaximumIntakeTrackerUrlBytes||fields.tiers.size()>MaximumIntakeTrackerTiers){return false;}
  std::size_t count=0,bytes=0;
  const auto text=[](std::string_view value){return std::all_of(value.begin(),value.end(),[](unsigned char c){return c>32&&c<127&&c!='\\';});};
  if(!text(fields.announce)){return false;}
  for(const auto& tier:fields.tiers){for(const auto& url:tier){
    if(++count>MaximumIntakeTrackers||url.empty()||url.size()>MaximumIntakeTrackerUrlBytes
        ||url.size()>MaximumIntakeTrackerBytes-bytes||!text(url)){return false;}bytes+=url.size();
  }}return true;
}
inline bool validTorrentIntakeTrackerTiers(const TorrentIntakeTrackerTiers& tiers) noexcept {
  if(tiers.size()>MaximumIntakeTrackerTiers){return false;}
  std::size_t count=0,bytes=0;
  for(const auto& tier:tiers){for(const auto& url:tier){
    if(++count>MaximumIntakeTrackers||url.empty()||url.size()>MaximumIntakeTrackerUrlBytes
        ||url.size()>MaximumIntakeTrackerBytes-bytes
        ||!std::all_of(url.begin(),url.end(),[](unsigned char c){return c>32&&c<127&&c!='\\';})){return false;}
    bytes+=url.size();
  }}
  for(const auto& tier:tiers){for(const auto& url:tier){
    const auto prefix=url.starts_with("http://")?7U:url.starts_with("https://")?8U:url.starts_with("udp://")?6U:0U;
    if(!prefix||url.size()<=prefix||url.find_first_of("/?#",prefix)==prefix){return false;}
  }}return true;
}
inline std::optional<TorrentIntakeTrackerTiers> decodeTorrentIntakeTrackerTiers(const Node& value) {
  if(value.type!=Node::Type::List||value.children.size()>MaximumIntakeTrackerTiers){return {};}
  TorrentIntakeTrackerTiers result;std::size_t count=0,bytes=0;
  for(const auto& tier:value.children){
    if(tier.type!=Node::Type::List){return {};}result.emplace_back();
    for(const auto& url:tier.children){
      if(url.type!=Node::Type::Bytes||++count>MaximumIntakeTrackers||url.bytes.size()>MaximumIntakeTrackerUrlBytes
          ||url.bytes.size()>MaximumIntakeTrackerBytes-bytes){return {};}
      bytes+=url.bytes.size();result.back().emplace_back(url.bytes);
    }
  }
  if(!validTorrentIntakeTrackerTiers(result)){return {};}return result;
}
} // namespace biglybtc::core
