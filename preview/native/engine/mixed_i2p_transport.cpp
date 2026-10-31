// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixed_i2p_transport.hpp"
#if __has_include(<libtorrent/biglybtc_mixed_i2p_transport.hpp>)
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#include <dlfcn.h>
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
void MixedI2pTransport::retire()noexcept{try{std::lock_guard lock(state_->mutex);state_->owner=nullptr;}catch(...){std::terminate();}}
std::expected<void,storage::Error> MixedI2pTransport::configure(const libtorrent::BiglybtcMixedI2pTransportPolicyV1& policy){
#if BIGLYBTC_HAS_MIXED_TRANSPORT_HEADER
  std::lock_guard lock(state_->mutex);if(!state_->owner||!state_->pin){return denied("Mixed I2P engine owner has retired");}
  const auto operation=reinterpret_cast<decltype(&libtorrent::biglybtc_configure_mixed_i2p_transport_v1)>(::dlsym(state_->pin.get(),"biglybtc_configure_mixed_i2p_transport_v1"));
  if(!operation){return denied("Mixed I2P provider export is unavailable");}
  return checked(operation(state_->owner,&policy));
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
