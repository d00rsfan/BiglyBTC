// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_interfaces.hpp"
#include "ip_socket.hpp"

#include <cstring>
#include <memory>
#include <utility>

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace biglybtc::platform {
namespace {

void readAddress(InterfaceAddress& result, const sockaddr& address) {
  if (address.sa_family == AF_INET) {
    const auto& inet = reinterpret_cast<const sockaddr_in&>(address);
    result.length = sizeof(inet.sin_addr);
    std::memcpy(result.address.data(), &inet.sin_addr, result.length);
  } else {
    const auto& inet = reinterpret_cast<const sockaddr_in6&>(address);
    result.length = sizeof(inet.sin6_addr);
    result.scope = inet.sin6_scope_id;
    std::memcpy(result.address.data(), &inet.sin6_addr, result.length);
  }
}

}

std::expected<std::vector<InterfaceAddress>, NetworkInterfacesError> readInterfaceAddresses(std::size_t maximum) {
  std::vector<InterfaceAddress> addresses;
  std::size_t visited = 0;
#ifdef _WIN32
  // IP Helper recommends starting with 15 KiB and retrying a bounded number of
  // times if the required size changes between snapshots.
  constexpr auto flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 15 * 1024;
  std::unique_ptr<std::byte[]> buffer;
  IP_ADAPTER_ADDRESSES* adapters = nullptr;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (size > 1024 * 1024) { return std::unexpected(NetworkInterfacesError::Limit); }
    buffer = std::make_unique<std::byte[]>(size);
    auto* entries = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());
    const auto status = ::GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, entries, &size);
    if (status == ERROR_NO_DATA) { return addresses; }
    if (status == NO_ERROR) { adapters = entries; break; }
    if (status != ERROR_BUFFER_OVERFLOW) { return std::unexpected(NetworkInterfacesError::Io); }
  }
  if (!adapters) { return std::unexpected(NetworkInterfacesError::Io); }
  std::size_t adapterCount = 0;
  for (const auto* adapter = adapters; adapter; adapter = adapter->Next) {
    if (++adapterCount > maximum) { return std::unexpected(NetworkInterfacesError::Limit); }
    if (!adapter->FirstUnicastAddress) { continue; }
    std::array<char, IF_MAX_STRING_SIZE + 1> name{};
    if (::ConvertInterfaceLuidToNameA(&adapter->Luid, name.data(), name.size()) != NO_ERROR) {
      return std::unexpected(NetworkInterfacesError::Io);
    }
    for (const auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
      if (++visited > maximum) { return std::unexpected(NetworkInterfacesError::Limit); }
      const auto* address = entry->Address.lpSockaddr;
      if (!address || entry->Address.iSockaddrLength < static_cast<int>(sizeof(address->sa_family))) { continue; }
      const bool ipv4 = address->sa_family == AF_INET;
      const bool ipv6 = address->sa_family == AF_INET6;
      if ((!ipv4 && !ipv6) || entry->Address.iSockaddrLength <
          static_cast<int>(ipv4 ? sizeof(sockaddr_in) : sizeof(sockaddr_in6))) { continue; }
      NET_IFINDEX index = ipv4 ? adapter->IfIndex : adapter->Ipv6IfIndex;
      if (!index && (::ConvertInterfaceLuidToIndex(&adapter->Luid, &index) != NO_ERROR || !index)) {
        return std::unexpected(NetworkInterfacesError::Io);
      }
      InterfaceAddress value{.name = name.data(), .index = index,
          .up = adapter->OperStatus == IfOperStatusUp, .loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK};
      readAddress(value, *address);
      addresses.push_back(std::move(value));
    }
  }
#else
  ifaddrs* head = nullptr;
  if (::getifaddrs(&head) != 0) { return std::unexpected(NetworkInterfacesError::Io); }
  const std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> owner(head, &::freeifaddrs);
  for (const auto* entry = head; entry; entry = entry->ifa_next) {
    if (++visited > maximum) { return std::unexpected(NetworkInterfacesError::Limit); }
    const auto* address = entry->ifa_addr;
    if (!address || !entry->ifa_name || (address->sa_family != AF_INET && address->sa_family != AF_INET6)) { continue; }
    const auto index = ::if_nametoindex(entry->ifa_name);
    if (!index) { return std::unexpected(NetworkInterfacesError::Io); }
    InterfaceAddress value{.name = entry->ifa_name, .index = index,
        .up = bool(entry->ifa_flags & IFF_UP), .loopback = bool(entry->ifa_flags & IFF_LOOPBACK)};
    readAddress(value, *address);
    addresses.push_back(std::move(value));
  }
#endif
  return addresses;
}

}
