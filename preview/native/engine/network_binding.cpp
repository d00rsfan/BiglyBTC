#include "network_binding.hpp"
#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <charconv>
#include <ifaddrs.h>
#include <memory>
#include <net/if.h>
#include <new>
#include <sys/socket.h>

namespace biglybtc::engine {
namespace {
using Code = NetworkPolicyErrorCode;
struct Failure { NetworkPolicyError error; };
void require(bool value, Code code, const char* field) { if (!value) { throw Failure{{code, field}}; } }
NetworkPolicyError unavailable() { return {Code::BindingUnavailable, "Bind IP interface unavailable"}; }
std::string_view trim(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 32) { value.remove_prefix(1); }
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 32) { value.remove_suffix(1); }
  return value;
}
bool interfaceName(std::string_view name) {
  return !name.empty() && name.size() < IFNAMSIZ && std::ranges::all_of(name, [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '.' || c == '-' || c == ':';
  });
}
bool explicitBindings(const NetworkPolicy& policy) {
  if (policy.mode == NetworkMode::IsolatedLoopback) { return false; }
  return std::ranges::any_of(policy.bindAddresses, [](const auto& text) { return text != "0.0.0.0" && text != "::"; });
}
bool v6(const NetworkInterfaceAddress& entry) { return entry.address.find(':') != std::string::npos; }
}
std::expected<NetworkBindSelector, NetworkPolicyError> parseNetworkBindSelector(std::string_view text) {
  try {
    text = trim(text); require(!text.empty() && text.size() <= 128, Code::InvalidConfiguration, "Bind IP");
    NetworkBindSelector result;
    auto address = text; std::optional<unsigned> scope;
    if (const auto percent = address.find('%'); percent != std::string_view::npos) {
      const auto value = address.substr(percent + 1); unsigned index = 0;
      const auto converted = std::from_chars(value.data(), value.data() + value.size(), index);
      require(!value.empty() && converted.ec == std::errc{} && converted.ptr == value.data() + value.size() && index,
          Code::Unsupported, "Bind IP IPv6 scope");
      scope = index; address = address.substr(0, percent);
    }
    std::array<unsigned char, 16> raw{}; std::array<char, INET6_ADDRSTRLEN> output{};
    const std::string input(address);
    const int family = ::inet_pton(AF_INET, input.c_str(), raw.data()) == 1 ? AF_INET : AF_INET6;
    if (::inet_pton(family, input.c_str(), raw.data()) == 1) {
      require(!scope || family == AF_INET6, Code::InvalidConfiguration, "Bind IP scope");
      require(::inet_ntop(family, raw.data(), output.data(), output.size()), Code::InvalidConfiguration, "Bind IP");
      result.numeric = true; result.canonical = output.data();
      if (scope) { result.canonical += '%' + std::to_string(*scope); }
      result.family = family == AF_INET ? NetworkBindFamily::V4 : NetworkBindFamily::V6;
      result.wildcard = !scope && (result.canonical == "0.0.0.0" || result.canonical == "::");
      return result;
    }
    require(!scope, Code::InvalidConfiguration, "Bind IP scope");
    const auto bracket = text.find('['); const auto name = text.substr(0, bracket);
    require(interfaceName(name), Code::Unsupported, "Bind IP interface selector");
    result.interfaceName = name; result.canonical = name;
    if (bracket != std::string_view::npos) {
      require(text.back() == ']' && text.find('[', bracket + 1) == std::string_view::npos,
          Code::InvalidConfiguration, "Bind IP interface selector");
      auto selector = text.substr(bracket + 1, text.size() - bracket - 2); std::string lower(selector);
      for (auto& c : lower) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); } }
      if (lower == "v4" || lower == "ipv4") { result.family = NetworkBindFamily::V4; result.canonical += "[v4]"; }
      else if (lower == "v6" || lower == "ipv6") { result.family = NetworkBindFamily::V6; result.canonical += "[v6]"; }
      else {
        unsigned index = 0; const auto converted = std::from_chars(selector.data(), selector.data() + selector.size(), index);
        require(!selector.empty() && converted.ec == std::errc{} && converted.ptr == selector.data() + selector.size()
            && index < MaximumNetworkInterfaceAddresses, Code::InvalidConfiguration, "Bind IP interface index");
        result.addressIndex = index; result.canonical += '[' + std::to_string(index) + ']';
      }
    }
    return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "Bind IP"}); }
}
std::expected<std::vector<NetworkInterfaceAddress>, NetworkPolicyError> readNetworkInterfaces() {
  try {
    ifaddrs* raw = nullptr; if (::getifaddrs(&raw) != 0) { return std::unexpected(unavailable()); }
    const std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> owner(raw, &::freeifaddrs);
    std::vector<NetworkInterfaceAddress> result; std::size_t visited = 0;
    for (auto* entry = raw; entry; entry = entry->ifa_next) {
      require(++visited <= MaximumNetworkInterfaceAddresses, Code::Limit, "Bind IP interface snapshot");
      if (!entry->ifa_addr || !entry->ifa_name || (entry->ifa_addr->sa_family != AF_INET && entry->ifa_addr->sa_family != AF_INET6)) { continue; }
      require(interfaceName(entry->ifa_name), Code::Unsupported, "Bind IP interface name");
      const auto index = ::if_nametoindex(entry->ifa_name); require(index != 0, Code::BindingUnavailable, "Bind IP interface identity");
      std::array<char, INET6_ADDRSTRLEN> value{}; const auto family = entry->ifa_addr->sa_family;
      const void* address = family == AF_INET ? static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr)
          : static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr)->sin6_addr);
      require(::inet_ntop(family, address, value.data(), value.size()), Code::BindingUnavailable, "Bind IP interface address");
      NetworkInterfaceAddress item{entry->ifa_name, value.data(), index, (entry->ifa_flags & IFF_UP) != 0};
      if (family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr)->sin6_addr)) {
        item.address += '%' + std::to_string(index);
      }
      if (std::ranges::find(result, item) == result.end()) { result.push_back(std::move(item)); }
    }
    return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "Bind IP interfaces"}); }
}
std::expected<NetworkBinding, NetworkPolicyError> resolveNetworkBinding(
    const NetworkPolicy& policy, std::span<const NetworkInterfaceAddress> interfaces) {
  try {
    if (auto valid = validateNetworkPolicy(policy); !valid) { return std::unexpected(valid.error()); }
    require(interfaces.size() <= MaximumNetworkInterfaceAddresses, Code::Limit, "Bind IP interface snapshot");
    NetworkBinding result{policy, {}};
    if (!explicitBindings(policy)) { return result; }
    result.effective.bindAddresses.clear();
    for (const auto& text : policy.bindAddresses) {
      const auto parsed = parseNetworkBindSelector(text); require(parsed.has_value(), Code::InvalidConfiguration, "Bind IP");
      require(!parsed->wildcard, Code::Unsupported, "Bind IP mixed wildcard");
      std::vector<NetworkInterfaceAddress> choices;
      for (const auto& item : interfaces) {
        require(interfaceName(item.name) && item.index, Code::InvalidConfiguration, "Bind IP interface snapshot");
        const auto numeric = parseNetworkBindSelector(item.address);
        require(numeric && numeric->numeric && !numeric->wildcard && numeric->canonical == item.address,
            Code::InvalidConfiguration, "Bind IP interface snapshot");
        if (!item.up || (!policy.ipv6 && v6(item) && !parsed->addressIndex)) { continue; }
        if (parsed->numeric ? item.address == parsed->canonical : item.name == parsed->interfaceName) {
          if (parsed->family == NetworkBindFamily::V4 && v6(item)) { continue; }
          if (parsed->family == NetworkBindFamily::V6 && !v6(item)) { continue; }
          if (std::ranges::find(choices, item) == choices.end()) { choices.push_back(item); }
        }
      }
      if (parsed->numeric) { require(choices.size() == 1, Code::BindingUnavailable, "Bind IP address not uniquely assigned"); }
      if (parsed->addressIndex) {
        require(*parsed->addressIndex < choices.size(), Code::BindingUnavailable, "Bind IP interface index unavailable");
        const auto selected = choices[*parsed->addressIndex];
        require(policy.ipv6 || !v6(selected), Code::Unsupported, "Bind IP index selects disabled IPv6");
        choices.assign(1, selected);
      }
      require(!choices.empty(), Code::BindingUnavailable, "Bind IP interface unavailable");
      for (auto& item : choices) {
        if (std::ranges::find(result.effective.bindAddresses, item.address) != result.effective.bindAddresses.end()) { continue; }
        require(result.pins.size() < MaximumResolvedNetworkBinds, Code::Limit, "Bind IP resolved addresses");
        result.effective.bindAddresses.push_back(item.address); result.pins.push_back(std::move(item));
      }
    }
    return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "Bind IP binding"}); }
}
bool networkBindingCurrent(const NetworkBinding& binding, std::span<const NetworkInterfaceAddress> interfaces) noexcept {
  if (interfaces.size() > MaximumNetworkInterfaceAddresses || binding.pins.size() > MaximumResolvedNetworkBinds) { return false; }
  return std::ranges::all_of(binding.pins, [&](const auto& pin) { return pin.up && pin.index
      && std::ranges::find(interfaces, pin) != interfaces.end(); });
}
std::expected<NetworkBinding, NetworkPolicyError> prepareNetworkBinding(const NetworkPolicy& policy) {
  if (auto valid = validateNetworkPolicy(policy); !valid) { return std::unexpected(valid.error()); }
  if (!explicitBindings(policy)) { return resolveNetworkBinding(policy, {}); }
  auto current = readNetworkInterfaces(); if (!current) { return std::unexpected(current.error()); }
  return resolveNetworkBinding(policy, *current);
}
bool networkBindingCurrent(const NetworkBinding& binding) noexcept {
  if (binding.pins.empty()) { return true; }
  try { const auto current = readNetworkInterfaces(); return current && networkBindingCurrent(binding, *current); }
  catch (...) { return false; }
}
}
