// Optional PRIVATE native-owner bridge. Not the published provider27 ABI.
#pragma once
#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include <array>
#include <cstdint>
#include <type_traits>

namespace libtorrent {
inline constexpr unsigned BiglybtcPrivateI2pOwnerAbiV1=0x42494f01;
struct BiglybtcPrivateI2pOwnerPolicyV1 {
  std::uint32_t version=1,size=sizeof(BiglybtcPrivateI2pOwnerPolicyV1);
  std::uint64_t generation=0,admission=0;
  // Configuration-only ephemeral secret; never returned by observations.
  std::array<unsigned char,32> capability{};
  std::array<unsigned char,32> public_destination_hash{};
  std::array<unsigned char,20> torrent_v1{};
  std::array<unsigned char,32> torrent_v2{};
  std::uint32_t hashes=0; // v1=1, v2=2; actual full hashes, not selected prefixes.
  std::uint16_t bridge_port=0,peer_port=0,local_peer_port=0,reserved=0;
};
struct BiglybtcPrivateI2pOwnerStatusV1 {
  std::uint32_t version=1,size=sizeof(BiglybtcPrivateI2pOwnerStatusV1);
  std::uint64_t generation=0,admission=0;
  std::array<unsigned char,32> public_destination_hash{};
  std::uint32_t phase=0; // 0 absent,1 configured,2 withdrawn. No readiness claim.
  std::uint16_t bridge_port=0,reserved=0;
  std::uint64_t reserved_counters[2]{};
};
static_assert(sizeof(BiglybtcPrivateI2pOwnerPolicyV1)==152);
static_assert(sizeof(BiglybtcPrivateI2pOwnerStatusV1)==80);
static_assert(std::is_trivially_copyable<BiglybtcPrivateI2pOwnerPolicyV1>::value
  &&std::is_standard_layout<BiglybtcPrivateI2pOwnerPolicyV1>::value);
enum class BiglybtcPrivateI2pOwnerResultV1:std::uint32_t {
  Accepted=0,InvalidArgument=1,Busy=2,InvalidHandle=3,Failed=4,Stale=5
};
// All three operations execute synchronously on the actual provider executor.
// Configure once, while SAM is disabled and before any torrent exists. The
// caller must separately hold the exact approved same-image provider pin.
extern "C" TORRENT_EXPORT unsigned biglybtc_private_i2p_owner_abi_v1() noexcept;
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_configure_private_i2p_owner_v1(
  session_handle const*,BiglybtcPrivateI2pOwnerPolicyV1 const*) noexcept;
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_read_private_i2p_owner_v1(
  session_handle const*,BiglybtcPrivateI2pOwnerStatusV1*) noexcept;
// Irreversible: invalidates shared socket contexts and aborts this dedicated
// session. Actual session join remains the external owner's responsibility.
extern "C" TORRENT_EXPORT std::uint32_t biglybtc_withdraw_private_i2p_owner_v1(
  session_handle const*,std::uint64_t generation) noexcept;
} // namespace libtorrent
