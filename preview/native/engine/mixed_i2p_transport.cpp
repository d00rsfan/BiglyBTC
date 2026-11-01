// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixed_i2p_transport.hpp"
#include <algorithm>
#if __has_include(<libtorrent/biglybtc_mixed_i2p_transport.hpp>)
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#include <dlfcn.h>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#define BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER 1
#else
#define BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER 0
#endif
namespace biglybtc::engine {
namespace {
std::expected<void,storage::Error> denied(const char* message){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,message});}
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
std::expected<void,storage::Error> checked(std::uint32_t value){
  if(value==static_cast<std::uint32_t>(libtorrent::BiglybtcMixedI2pResultV1::Accepted)){return {};}
  return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Mixed I2P provider rejected a current route operation"});
}
libtorrent::BiglybtcMixedI2pRouteV1 routeFor(const MixedManagedSource& source,std::uint64_t generation,std::uint64_t admission){
  libtorrent::BiglybtcMixedI2pRouteV1 route;route.generation=generation;route.admission=admission;
  const auto& hashes=source.identity().full;
  if(hashes.v1){route.hashes|=1;for(std::size_t i=0;i<route.torrent_v1.size();++i){route.torrent_v1[i]=std::to_integer<unsigned char>((*hashes.v1)[i]);}}
  if(hashes.v2){route.hashes|=2;for(std::size_t i=0;i<route.torrent_v2.size();++i){route.torrent_v2[i]=std::to_integer<unsigned char>((*hashes.v2)[i]);}}
  return route;
}
#endif
}
void MixedI2pTransport::retire()noexcept{try{std::lock_guard lock(state_->mutex);state_->configured.store(false);state_->owner=nullptr;}catch(...){std::terminate();}}
std::expected<void,storage::Error> MixedI2pTransport::configure(const libtorrent::BiglybtcMixedI2pTransportPolicyV1& policy){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
  std::lock_guard lock(state_->mutex);if(!state_->owner||!state_->pin){return denied("Mixed I2P engine owner has retired");}
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_configure_mixed_i2p_transport_v1)>(::dlsym(state_->pin.get(),"biglybtc_configure_mixed_i2p_transport_v1"));
  if(!operation){return denied("Mixed I2P provider export is unavailable");}
  auto result=checked(operation(state_->owner,&policy));
  if(result){state_->configured.store(true);}
  return result;
#else
  (void)policy;return denied("Mixed I2P SDK is unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::admit(const MixedManagedSource& source,std::uint64_t generation,std::uint64_t admission){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
  std::lock_guard lock(state_->mutex);if(!state_->origin||source.origin_!=state_->origin){return denied("Mixed source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin||!source.backendCurrent()){return denied("Mixed I2P admission is not attached to its actual engine handle");}
  const auto route=routeFor(source,generation,admission);
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_admit_mixed_i2p_route_v1)>(::dlsym(state_->pin.get(),"biglybtc_admit_mixed_i2p_route_v1"));
  if(!operation){return denied("Mixed I2P provider export is unavailable");}
  return checked(operation(state_->owner,&route));
#else
  (void)source;(void)generation;(void)admission;return denied("Mixed I2P SDK is unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::revoke(const MixedManagedSource& source,std::uint64_t generation,std::uint64_t admission){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
  std::lock_guard lock(state_->mutex);if(!state_->origin||source.origin_!=state_->origin){return denied("Mixed source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin){return denied("Mixed I2P engine owner has retired");}
  const auto route=routeFor(source,generation,admission);
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_revoke_mixed_i2p_route_v1)>(::dlsym(state_->pin.get(),"biglybtc_revoke_mixed_i2p_route_v1"));
  if(!operation){return denied("Mixed I2P provider export is unavailable");}
  return checked(operation(state_->owner,&route));
#else
  (void)source;(void)generation;(void)admission;return denied("Mixed I2P SDK is unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::admitMetadata(const PendingMetadataSource& source,std::uint64_t generation,std::uint64_t admission){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER && defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  std::lock_guard lock(state_->mutex);
  if(!state_->origin||source.origin_!=state_->origin){return denied("Pending source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin||!state_->configured.load()||!source.effectsCurrent()||!generation||!admission||source.intent().network!=MagnetNetwork::PublicAndI2p||source.lifetime_->attached.load()||source.lifetime_->authorized.load()){return denied("Pending metadata route is not a fresh prepared engine admission");}
  libtorrent::BiglybtcMixedI2pRouteV1 route;route.generation=generation;route.admission=admission;
  if(source.requested().has_v1()){route.hashes|=1;std::copy(source.requested().v1.begin(),source.requested().v1.end(),route.torrent_v1.begin());}
  if(source.requested().has_v2()){route.hashes|=2;std::copy(source.requested().v2.begin(),source.requested().v2.end(),route.torrent_v2.begin());}
  const auto admit=reinterpret_cast<decltype(&libtorrent::biglybtc_admit_mixed_i2p_route_v1)>(::dlsym(state_->pin.get(),"biglybtc_admit_mixed_i2p_route_v1"));
  const auto revoke=reinterpret_cast<decltype(&libtorrent::biglybtc_revoke_mixed_i2p_route_v1)>(::dlsym(state_->pin.get(),"biglybtc_revoke_mixed_i2p_route_v1"));
  const auto authorize=reinterpret_cast<decltype(&libtorrent::biglybtc_authorize_i2p_metadata_v1)>(::dlsym(state_->pin.get(),"biglybtc_authorize_i2p_metadata_v1"));
  if(!admit||!revoke||!authorize){return denied("Qualified metadata provider exports unavailable");}
  auto accepted=checked(admit(state_->owner,&route));if(!accepted){return accepted;}
  libtorrent::BiglybtcPrivateI2pTorrentV1 tuple;tuple.generation=generation;tuple.admission=admission;tuple.hashes=route.hashes;tuple.torrent_v1=route.torrent_v1;tuple.torrent_v2=route.torrent_v2;
  if(authorize(state_->owner,&tuple)!=0||!source.effectsCurrent()){(void)revoke(state_->owner,&route);return denied("Provider refused the exact pending metadata authorization");}
  source.lifetime_->generation.store(generation);source.lifetime_->route.store(admission);source.lifetime_->authorized.store(true,std::memory_order_release);return {};
#else
  (void)source;(void)generation;(void)admission;return denied("Qualified metadata SDK is unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::revokeMetadata(const PendingMetadataSource& source,std::uint64_t generation,std::uint64_t admission){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER && defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  std::lock_guard lock(state_->mutex);if(!state_->origin||source.origin_!=state_->origin){return denied("Pending source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin||source.lifetime_->generation.load()!=generation||source.lifetime_->route.load()!=admission){return denied("Pending metadata route tuple changed");}
  libtorrent::BiglybtcMixedI2pRouteV1 route;route.generation=generation;route.admission=admission;
  if(source.requested().has_v1()){route.hashes|=1;std::copy(source.requested().v1.begin(),source.requested().v1.end(),route.torrent_v1.begin());}
  if(source.requested().has_v2()){route.hashes|=2;std::copy(source.requested().v2.begin(),source.requested().v2.end(),route.torrent_v2.begin());}
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_revoke_mixed_i2p_route_v1)>(::dlsym(state_->pin.get(),"biglybtc_revoke_mixed_i2p_route_v1"));
  if(!operation){return denied("Qualified metadata provider export unavailable");}
  auto result=checked(operation(state_->owner,&route));if(result){source.lifetime_->authorized.store(false);}return result;
#else
  (void)source;(void)generation;(void)admission;return denied("Qualified metadata SDK is unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::discoveredPeer(const MixedManagedSource& source,std::uint64_t generation,std::uint64_t route,const std::array<std::uint8_t,32>& peer,std::uint64_t operation){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER && defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  std::lock_guard lock(state_->mutex);if(!state_->origin||source.origin_!=state_->origin){return denied("Mixed source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin||!state_->configured.load()||!source.backendCurrent()||!generation||!route||!operation){return denied("Mixed discovered-peer source is no longer current");}
  const auto value=routeFor(source,generation,route);libtorrent::BiglybtcI2pDiscoveredPeerV1 request;
  request.torrent.generation=generation;request.torrent.admission=route;request.torrent.hashes=value.hashes;request.torrent.torrent_v1=value.torrent_v1;request.torrent.torrent_v2=value.torrent_v2;request.peer_hash=peer;request.operation=operation;
  const auto deliver=reinterpret_cast<decltype(&libtorrent::biglybtc_discovered_i2p_peer_v1)>(::dlsym(state_->pin.get(),"biglybtc_discovered_i2p_peer_v1"));
  if(!deliver){return denied("Qualified discovered-peer provider unavailable");}return checked(deliver(state_->owner,&request));
#else
  (void)source;(void)generation;(void)route;(void)peer;(void)operation;return denied("Qualified discovery SDK unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::discoveredPeer(const PendingMetadataSource& source,std::uint64_t generation,std::uint64_t route,const std::array<std::uint8_t,32>& peer,std::uint64_t operation){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER && defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  std::lock_guard lock(state_->mutex);if(!state_->origin||source.origin_!=state_->origin){return denied("Pending source belongs to another actual engine owner");}
  if(!state_->owner||!state_->pin||!state_->configured.load()||!source.effectsCurrent()||!source.lifetime_->attached.load()||!source.lifetime_->authorized.load()||source.lifetime_->generation.load()!=generation||source.lifetime_->route.load()!=route||!operation){return denied("Pending discovered-peer source is no longer current");}
  libtorrent::BiglybtcI2pDiscoveredPeerV1 request;request.torrent.generation=generation;request.torrent.admission=route;request.peer_hash=peer;request.operation=operation;
  if(source.requested().has_v1()){request.torrent.hashes|=1;std::copy(source.requested().v1.begin(),source.requested().v1.end(),request.torrent.torrent_v1.begin());}
  if(source.requested().has_v2()){request.torrent.hashes|=2;std::copy(source.requested().v2.begin(),source.requested().v2.end(),request.torrent.torrent_v2.begin());}
  const auto deliver=reinterpret_cast<decltype(&libtorrent::biglybtc_discovered_i2p_peer_v1)>(::dlsym(state_->pin.get(),"biglybtc_discovered_i2p_peer_v1"));
  if(!deliver){return denied("Qualified discovered-peer provider unavailable");}return checked(deliver(state_->owner,&request));
#else
  (void)source;(void)generation;(void)route;(void)peer;(void)operation;return denied("Qualified discovery SDK unavailable");
#endif
}
std::expected<void,storage::Error> MixedI2pTransport::withdraw(std::uint64_t generation){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
  std::lock_guard lock(state_->mutex);if(!state_->owner||!state_->pin){return denied("Mixed I2P engine owner has retired");}
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_withdraw_mixed_i2p_transport_v1)>(::dlsym(state_->pin.get(),"biglybtc_withdraw_mixed_i2p_transport_v1"));
  if(!operation){return denied("Mixed I2P provider export is unavailable");}
  return checked(operation(state_->owner,generation));
#else
  (void)generation;return denied("Mixed I2P SDK is unavailable");
#endif
}
}
