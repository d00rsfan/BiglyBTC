// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_policy.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/alert.hpp>
#include <algorithm>
namespace biglybtc::engine {
namespace {
bool tracker(std::string_view url){
  if(!url.starts_with("http://")||url.size()>2048||url.find_first_of("\r\n\t ")!=std::string_view::npos||url.find('\0')!=std::string_view::npos){return false;}
  auto host=url.substr(7);host=host.substr(0,host.find_first_of("/:?#"));
  if(host.size()!=60||!host.ends_with(".b32.i2p")){return false;}
  return std::all_of(host.begin(),host.begin()+52,[](char c){return (c>='a'&&c<='z')||(c>='2'&&c<='7');});
}
}
bool OwnedI2pPolicy::matches(const storage::ProfileLease& lease,const core::TorrentHash& hash)const noexcept{return lease_&&lease_->sharesOwnership(lease)&&hash==hash_;}
libtorrent::settings_pack OwnedI2pPolicy::settings()const{
  namespace lt=libtorrent;lt::settings_pack p;
  for(int key:{lt::settings_pack::enable_incoming_tcp,lt::settings_pack::enable_outgoing_tcp,lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::allow_i2p_mixed}){p.set_bool(key,false);}
  p.set_str(lt::settings_pack::listen_interfaces,"");p.set_str(lt::settings_pack::outgoing_interfaces,"");
  p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");p.set_str(lt::settings_pack::i2p_hostname,"");p.set_int(lt::settings_pack::i2p_port,0);
  p.set_bool(lt::settings_pack::seeding_outgoing_connections,true);
  p.set_int(lt::settings_pack::proxy_type,lt::settings_pack::none);p.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::all));return p;
}
std::expected<void,NetworkPolicyError> OwnedI2pPolicy::apply(const TorrentNetworkSelection& selection,libtorrent::add_torrent_params& params)const{
  const auto bad=[]()->std::expected<void,NetworkPolicyError>{return std::unexpected(NetworkPolicyError{NetworkPolicyErrorCode::Unsupported,"explicit owned I2P-only torrent"});};
  if(!lease_||!lease_->valid()||selection.publicNetwork||selection.tor||!selection.i2p||!params.ti||!params.peers.empty()||!params.dht_nodes.empty()||!params.url_seeds.empty()||!params.http_seeds.empty()||!params.ti->web_seeds().empty()){return bad();}
  if(params.trackers.size()>4||params.ti->trackers().size()>4){return bad();}
  for(const auto& entry:params.trackers){if(!tracker(entry)){return bad();}}
  for(const auto& entry:params.ti->trackers()){if(!tracker(entry.url)){return bad();}}
  auto next=params;next.flags|=libtorrent::torrent_flags::disable_dht|libtorrent::torrent_flags::disable_lsd|libtorrent::torrent_flags::disable_pex|libtorrent::torrent_flags::override_web_seeds;
  next.flags&=~libtorrent::torrent_flags::auto_managed;next.flags|=libtorrent::torrent_flags::paused;
  params=std::move(next);return {};
}
}
