// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixed_i2p_source.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/announce_entry.hpp>
#include <algorithm>
#include <charconv>
#include <map>

namespace biglybtc::engine {
namespace {
bool alternativeTracker(std::string_view url){
  const auto scheme=url.find("://");if(scheme==std::string_view::npos){return false;}
  auto authority=url.substr(scheme+3);authority=authority.substr(0,authority.find_first_of("/?#"));
  auto host=authority.substr(0,authority.find(':'));
  return host.ends_with(".i2p");
}
bool supportedI2pTracker(std::string_view url){
  if(!url.starts_with("http://")||url.size()>2048||url.find_first_of("\r\n\t ")!=std::string_view::npos||url.find('\0')!=std::string_view::npos||url.find('#')!=std::string_view::npos){return false;}
  auto authority=url.substr(7);authority=authority.substr(0,authority.find_first_of("/?"));
  const auto colon=authority.find(':');const auto host=authority.substr(0,colon);
  if(host.size()!=60||!host.ends_with(".b32.i2p")||!std::all_of(host.begin(),host.begin()+52,[](char c){return (c>='a'&&c<='z')||(c>='2'&&c<='7');})){return false;}
  if(colon!=std::string_view::npos){const auto part=authority.substr(colon+1);unsigned port=0;const auto parsed=std::from_chars(part.data(),part.data()+part.size(),port);if(part.empty()||parsed.ec!=std::errc{}||parsed.ptr!=part.data()+part.size()||port==0||port>65535){return false;}}
  return true;
}
}
std::expected<void,NetworkPolicyError> MixedPublicPolicy::apply(const NetworkPolicy& policy,
    const TorrentNetworkSelection& selection,libtorrent::add_torrent_params& params)const{
  const auto bad=[](const char* message)->std::expected<void,NetworkPolicyError>{return std::unexpected(NetworkPolicyError{NetworkPolicyErrorCode::Unsupported,message});};
  if(!lease_||!lease_->valid()||!selection.publicNetwork||!selection.i2p||selection.tor||!params.ti){return bad("Explicit Public+I2P source required");}
  if(params.trackers.size()>2048||params.ti->trackers().size()>2048){return bad("Mixed tracker inventory exceeds bound");}
  auto next=params;const auto previousFlags=next.flags;
  const int last=next.tracker_tiers.empty()?0:next.tracker_tiers.back();
  if(next.tracker_tiers.size()>next.trackers.size()){return bad("Mixed tracker tiers are malformed");}
  next.tracker_tiers.resize(next.trackers.size(),last);
  if(!(previousFlags&libtorrent::torrent_flags::override_trackers)){
    for(const auto& entry:next.ti->trackers()){
      if(std::ranges::find(next.trackers,entry.url)==next.trackers.end()){next.trackers.push_back(entry.url);next.tracker_tiers.push_back(entry.tier);}
    }
  }
  if(next.trackers.size()>2048){return bad("Merged mixed tracker inventory exceeds bound");}
  const auto all=next.trackers;const auto tiers=next.tracker_tiers;
  next.trackers.clear();next.tracker_tiers.clear();
  for(std::size_t i=0;i<all.size();++i){
    if(alternativeTracker(all[i])){/* Alternative-network endpoints never enter Public URL/DNS policy. */}
    else{next.trackers.push_back(all[i]);next.tracker_tiers.push_back(tiers[i]);}
  }
  next.flags|=libtorrent::torrent_flags::override_trackers;
  auto publicSelection=selection;publicSelection.i2p=false;
  auto applied=applyTorrentNetworkPolicy(policy,publicSelection,next);if(!applied){return applied;}
  const auto acceptedPublic=next.trackers;next.trackers.clear();next.tracker_tiers.clear();
  for(std::size_t i=0;i<all.size();++i){
    if((alternativeTracker(all[i])&&supportedI2pTracker(all[i])&&policy.httpTrackers)
        ||std::ranges::find(acceptedPublic,all[i])!=acceptedPublic.end()){
      next.trackers.push_back(all[i]);next.tracker_tiers.push_back(tiers[i]);
    }
  }
  // IPv4 PEX cannot carry I2P destinations. Until the original I2P PEX adapter
  // is qualified, don't disclose an alternative-network peer as a Public one.
  next.flags|=libtorrent::torrent_flags::disable_pex;
  params=std::move(next);return {};
}
}
