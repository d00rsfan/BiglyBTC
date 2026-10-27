#pragma once

#include "network_policy.hpp"

#include <libtorrent/address.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/peer_class.hpp>
#include <libtorrent/session_handle.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::engine {

inline int networkLanRate(int rate) noexcept {
  return rate == 0 ? 0 : std::clamp(rate, 10, std::numeric_limits<int>::max() - 1);
}

inline std::string javaProxyNumericAddress(const libtorrent::address& address) {
  if (address.is_v4()) { return address.to_v4().to_string(); }
  const auto bytes = address.to_v6().to_bytes();
  if (std::ranges::all_of(bytes.begin(), bytes.begin() + 10, [](auto byte) { return byte == 0; })
      && bytes[10] == 255 && bytes[11] == 255) {
    return libtorrent::address_v4({bytes[12], bytes[13], bytes[14], bytes[15]}).to_string();
  }
  std::string result;
  for (std::size_t i = 0; i < bytes.size(); i += 2) {
    if (i) { result += ':'; }
    std::array<char, 4> text{};
    const auto value = static_cast<unsigned>(bytes[i]) * 256U + bytes[i + 1];
    const auto end = std::to_chars(text.data(), text.data() + text.size(), value, 16).ptr;
    result.append(text.data(), end);
  }
  return result;
}

inline std::expected<libtorrent::ip_filter, NetworkPolicyError>
makeNetworkPeerClassFilter(const NetworkPolicy& policy) {
  if (auto valid = validateNetworkPolicy(policy); !valid) { return std::unexpected(valid.error()); }
  try {
    namespace lt = libtorrent;
    constexpr auto global = 1U << static_cast<unsigned>(lt::session_handle::global_peer_class_id);
    constexpr auto local = 1U << static_cast<unsigned>(lt::session_handle::local_peer_class_id);
    lt::ip_filter result;
    const auto add = [&](const char* first, const char* last, unsigned classes) {
      result.add_rule(lt::make_address(first), lt::make_address(last), classes);
    };
    add("0.0.0.0", "255.255.255.255", global);
    add("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", global);
    if (policy.lanRates) {
      // cb8 ClientInstanceManagerImpl::isLANAddress default well-known LANs:
      // InetAddress loopback/link-local/site-local. Java's IPv6 site-local is
      // fec0::/10; fc00::/7 ULA is not implicitly a LAN in this source.
      constexpr std::array ranges{
          std::pair{"10.0.0.0", "10.255.255.255"},
          std::pair{"172.16.0.0", "172.31.255.255"},
          std::pair{"192.168.0.0", "192.168.255.255"},
          std::pair{"169.254.0.0", "169.254.255.255"},
          std::pair{"127.0.0.0", "127.255.255.255"},
          std::pair{"::1", "::1"},
          std::pair{"fe80::", "febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff"},
          std::pair{"fec0::", "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"},
          std::pair{"::ffff:10.0.0.0", "::ffff:10.255.255.255"},
          std::pair{"::ffff:172.16.0.0", "::ffff:172.31.255.255"},
          std::pair{"::ffff:192.168.0.0", "::ffff:192.168.255.255"},
          std::pair{"::ffff:169.254.0.0", "::ffff:169.254.255.255"},
          std::pair{"::ffff:127.0.0.0", "::ffff:127.255.255.255"}};
      for (const auto& [first, last] : ranges) { add(first, last, local); }
      // cb8 excludes a configured numeric data SOCKS proxy from local rates.
      // Do not resolve hostnames here or widen the routing/DNS policy.
      if (policy.proxy.peers) {
        lt::error_code error;
        const auto address = lt::make_address(policy.proxy.host, error);
        // Original matches raw configured host strings against Java's
        // getHostAddress(): expanded lowercase IPv6 and unmapped IPv4 text.
        if (!error && policy.proxy.host == javaProxyNumericAddress(address)) {
          result.add_rule(address, address, global);
        }
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(NetworkPolicyError{NetworkPolicyErrorCode::Memory, "LAN peer classes"});
  }
}

inline void applyNetworkLanRates(libtorrent::session_handle& session, const NetworkPolicy& policy) {
  auto local = session.get_peer_class(libtorrent::session_handle::local_peer_class_id);
  local.upload_limit = networkLanRate(policy.lanUploadBytesPerSecond);
  local.download_limit = networkLanRate(policy.lanDownloadBytesPerSecond);
  session.set_peer_class(libtorrent::session_handle::local_peer_class_id, local);
}

} // namespace biglybtc::engine
