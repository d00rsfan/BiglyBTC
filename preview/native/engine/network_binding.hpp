#pragma once
#include "network_policy.hpp"
#include <optional>
#include <span>
#include <string_view>

namespace biglybtc::engine {
enum class NetworkBindFamily { Any, V4, V6 };
struct NetworkBindSelector {
  std::string canonical, interfaceName;
  NetworkBindFamily family = NetworkBindFamily::Any;
  std::optional<unsigned> addressIndex;
  bool numeric = false, wildcard = false;
};
// Pure original selector grammar, numeric addresses only (never DNS).
std::expected<NetworkBindSelector, NetworkPolicyError> parseNetworkBindSelector(std::string_view);
struct NetworkInterfaceAddress {
  std::string name, address;
  unsigned index = 0;
  bool up = false;
  bool operator==(const NetworkInterfaceAddress&) const = default;
};
struct NetworkBinding {
  NetworkPolicy effective; // Only resolved numeric endpoints reach LT.
  std::vector<NetworkInterfaceAddress> pins;
};
constexpr std::size_t MaximumNetworkInterfaceAddresses = 4096;
constexpr std::size_t MaximumResolvedNetworkBinds = 64;
// getifaddrs/if_nametoindex only, worker thread; no DNS/routes/socket probes.
std::expected<std::vector<NetworkInterfaceAddress>, NetworkPolicyError> readNetworkInterfaces();
// Pure snapshot resolution. Index is this OS snapshot's enumeration order;
// family selectors avoid Java's also explicitly unstable numeric ordering.
std::expected<NetworkBinding, NetworkPolicyError> resolveNetworkBinding(
    const NetworkPolicy&, std::span<const NetworkInterfaceAddress>);
// Every pinned address, UP state and interface index must still match. Additions
// never widen the accepted LT addresses. No implicit rebind or wildcard fallback.
bool networkBindingCurrent(const NetworkBinding&, std::span<const NetworkInterfaceAddress>) noexcept;
std::expected<NetworkBinding, NetworkPolicyError> prepareNetworkBinding(const NetworkPolicy&);
bool networkBindingCurrent(const NetworkBinding&) noexcept;
}
