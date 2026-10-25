#include "network_policy.hpp"
#include "network_binding.hpp"

#include <libtorrent/torrent_info.hpp>

#include <arpa/inet.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace biglybtc::engine {
namespace {
using Code = NetworkPolicyErrorCode;
using Node = core::Node;
using Settings = libtorrent::settings_pack;
constexpr std::size_t MaximumSources = 2048;
constexpr std::size_t MaximumText = 4096;
struct Failure { NetworkPolicyError error; };
[[noreturn]] void fail(Code code, std::string_view field) { throw Failure{{code, std::string(field)}}; }
void require(bool condition, Code code, std::string_view field) { if (!condition) { fail(code, field); } }
std::string_view trim(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 32) { value.remove_prefix(1); }
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 32) { value.remove_suffix(1); }
  return value;
}
std::string lower(std::string_view value) {
  std::string result(value);
  for (char& c : result) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); } }
  return result;
}
bool printable(std::string_view value) {
  return std::ranges::none_of(value, [](unsigned char c) { return c < 32 || c == 127; });
}
bool alternativeNetwork(std::string_view host) {
  auto text = lower(host);
  while (!text.empty() && text.back() == '.') { text.pop_back(); }
  return text == "onion" || text == "i2p" || text == "onion.alt" || text == "i2p.alt"
      || text.ends_with(".onion") || text.ends_with(".i2p")
      || text.ends_with(".onion.alt") || text.ends_with(".i2p.alt");
}
std::string numericAddress(std::string_view value, std::string_view field) {
  require(!value.empty() && value.size() < INET6_ADDRSTRLEN, Code::Unsupported, field);
  unsigned char storage[16]{};
  const std::string input(value);
  char output[INET6_ADDRSTRLEN]{};
  const int family = ::inet_pton(AF_INET, input.c_str(), storage) == 1 ? AF_INET : AF_INET6;
  require(::inet_pton(family, input.c_str(), storage) == 1, Code::Unsupported, field);
  require(::inet_ntop(family, storage, output, sizeof(output)) != nullptr, Code::InvalidConfiguration, field);
  return output;
}
void hostName(std::string_view host, std::string_view field) {
  require(!host.empty() && host.size() <= 253 && printable(host), Code::InvalidConfiguration, field);
  require(!alternativeNetwork(host), Code::Unsupported, field);
  if (host.find(':') != std::string_view::npos) { (void)numericAddress(host, field); return; }
  require(std::ranges::all_of(host, [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
  }), Code::Unsupported, field);
  require(host.front() != '.' && host.front() != '-' && host.find("..") == std::string_view::npos,
      Code::InvalidConfiguration, field);
}
int portNumber(std::string_view text, std::string_view field) {
  text = trim(text);
  if (!text.empty() && text.front() == '+') { text.remove_prefix(1); }
  unsigned int port = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
  require(!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size()
      && port > 0 && port <= 65535, Code::InvalidConfiguration, field);
  return static_cast<int>(port);
}
class Config {
public:
  explicit Config(const core::Document* document) : document_(document) {
    if (document) {
      require(document->root().type == Node::Type::Dictionary, Code::InvalidConfiguration, "biglybt.config");
      require(document->originalBytes().size() <= 4 * 1024 * 1024, Code::Limit, "biglybt.config");
    }
  }
  const Node* find(std::string_view key) const {
    if (!document_) { return nullptr; }
    const Node* found = nullptr;
    const auto& children = document_->root().children;
    for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
      if (children[i].bytes == key) {
        require(!found, Code::InvalidConfiguration, key);
        found = &children[i + 1];
      }
    }
    return found;
  }
  std::int64_t integer(std::string_view key, std::int64_t fallback) const {
    const auto* node = find(key);
    if (!node) { return fallback; }
    require(node->type == Node::Type::Integer, Code::InvalidConfiguration, key);
    return node->integer;
  }
  bool boolean(std::string_view key, bool fallback) const {
    const auto value = integer(key, fallback ? 1 : 0);
    require(value == 0 || value == 1, Code::InvalidConfiguration, key);
    return value == 1;
  }
  std::string text(std::string_view key, std::string_view fallback = {}) const {
    const auto* node = find(key);
    if (!node) { return std::string(fallback); }
    require(node->type == Node::Type::Bytes, Code::InvalidConfiguration, key);
    require(node->bytes.size() <= MaximumText, Code::Limit, key);
    require(printable(node->bytes), Code::InvalidConfiguration, key);
    return std::string(node->bytes);
  }
  int port(std::string_view key, int fallback) const {
    const auto value = integer(key, fallback);
    require(value >= 1 && value <= 65535, Code::InvalidConfiguration, key);
    return static_cast<int>(value);
  }
  int rate(std::string_view key) const {
    const auto value = integer(key, 0);
    // Never wrap a Java long or silently turn an unrepresentable finite limit
    // into LT's unlimited sentinel. Original negative values mean unlimited.
    if (value <= 0) { return 0; }
    require(value <= std::numeric_limits<int>::max() / 1024, Code::Limit, key);
    return static_cast<int>(value * 1024);
  }
private:
  const core::Document* document_;
};
std::string username(const Config& config, std::string_view key) {
  auto value = config.text(key, "<none>");
  if (lower(trim(value)) == "<none>") { value.clear(); }
  return value;
}
NetworkProxy proxyEndpoint(const Config& config, bool data, bool same, NetworkProxyKind kind) {
  const auto prefix = data && !same ? "Proxy.Data." : "Proxy.";
  NetworkProxy proxy;
  proxy.kind = kind;
  proxy.host = std::string(trim(config.text(std::string(prefix) + "Host")));
  hostName(proxy.host, std::string(prefix) + "Host");
  proxy.port = portNumber(config.text(std::string(prefix) + "Port"), std::string(prefix) + "Port");
  proxy.username = username(config, std::string(prefix) + "Username");
  proxy.password = config.text(std::string(prefix) + "Password");
  require(proxy.username.size() <= 1024 && proxy.password.size() <= 1024, Code::Limit, "Proxy credentials");
  require(!proxy.username.empty() || proxy.password.empty(), Code::Unsupported, "Proxy.Password");
  return proxy;
}
bool sameProxy(const NetworkProxy& a, const NetworkProxy& b) {
  return a.kind == b.kind && lower(a.host) == lower(b.host) && a.port == b.port
      && a.username == b.username && a.password == b.password;
}
void validate(const NetworkPolicy& p) {
  require(p.mode == NetworkMode::IsolatedLoopback || p.mode == NetworkMode::Normal,
      Code::InvalidConfiguration, "mode");
  require(p.listenPort >= 0 && p.listenPort <= 65535 && !p.bindAddresses.empty()
      && p.bindAddresses.size() <= MaximumResolvedNetworkBinds, Code::InvalidConfiguration, "Bind IP");
  for (const auto& address : p.bindAddresses) {
    const auto selected = parseNetworkBindSelector(address);
    require(selected && selected->canonical == address, Code::InvalidConfiguration, "Bind IP");
    require(p.ipv6 || selected->family != NetworkBindFamily::V6, Code::Unsupported, "IPV6 Enable Support");
    require(!p.enforceBind || !selected->wildcard, Code::Unsupported, "Enforce Bind IP wildcard");
  }
  require(p.uploadBytesPerSecond >= 0 && p.downloadBytesPerSecond >= 0 && p.seedingUploadBytesPerSecond >= 0,
      Code::InvalidConfiguration, "rate limits");
  const auto& proxy = p.proxy;
  require(proxy.kind == NetworkProxyKind::None || proxy.kind == NetworkProxyKind::Socks4
      || proxy.kind == NetworkProxyKind::Socks5 || proxy.kind == NetworkProxyKind::Http,
      Code::InvalidConfiguration, "proxy type");
  if (proxy.kind == NetworkProxyKind::None) {
    require(proxy.host.empty() && proxy.username.empty() && proxy.password.empty() && proxy.port == 0
        && !proxy.peers && !proxy.trackers && !proxy.remoteDns, Code::InvalidConfiguration, "proxy");
  } else {
    hostName(proxy.host, "Proxy.Host");
    require(proxy.port > 0 && proxy.port <= 65535 && (proxy.peers || proxy.trackers)
        && proxy.username.size() <= 1024 && proxy.password.size() <= 1024
        && printable(proxy.username) && printable(proxy.password)
        && (!proxy.username.empty() || proxy.password.empty()), Code::InvalidConfiguration, "proxy");
    if (proxy.kind == NetworkProxyKind::Socks4) {
      require(!proxy.remoteDns && proxy.password.empty() && !p.ipv6, Code::Unsupported, "SOCKS4 routing");
    }
    if (proxy.kind != NetworkProxyKind::Socks5) {
      require(!p.dht && (!proxy.peers || (!p.incomingUtp && !p.outgoingUtp))
          && (!proxy.trackers || !p.udpTrackers), Code::Unsupported, "proxy UDP routing");
    }
    require(!p.dht || proxy.peers, Code::Unsupported, "proxy DHT routing");
    // LT replaces peer-proxied listen interfaces with one proxy endpoint.
    // Do not silently discard a user's explicit source-address restriction.
    if (proxy.peers) {
      require(!p.upnp && !p.natpmp && !p.lsd, Code::Unsupported, "proxy discovery");
      require(p.bindAddresses == (p.ipv6 ? std::vector<std::string>{"0.0.0.0", "::"}
          : std::vector<std::string>{"0.0.0.0"}), Code::Unsupported, "proxy Bind IP");
    }
  }
  if (p.mode == NetworkMode::IsolatedLoopback) {
    require(p.bindAddresses == std::vector<std::string>{"127.0.0.1"} && p.listenPort == 0
        && !p.ipv6 && !p.incomingUtp && !p.outgoingUtp && !p.httpTrackers && !p.udpTrackers
        && !p.dht && !p.pex && !p.lsd && !p.webSeeds && !p.upnp && !p.natpmp
        && proxy.kind == NetworkProxyKind::None, Code::InvalidConfiguration, "isolated policy");
  }
}
enum class UrlKind { Http, Https, Udp };
UrlKind urlKind(std::string_view url, std::string_view field) {
  require(!url.empty() && url.size() <= MaximumText && printable(url)
      && url.find(' ') == std::string_view::npos && url.find('\\') == std::string_view::npos,
      Code::InvalidConfiguration, field);
  const auto separator = url.find("://");
  require(separator != std::string_view::npos, Code::Unsupported, field);
  const auto scheme = lower(url.substr(0, separator));
  const auto kind = scheme == "http" ? UrlKind::Http : scheme == "https" ? UrlKind::Https : UrlKind::Udp;
  require(scheme == "http" || scheme == "https" || scheme == "udp", Code::Unsupported, field);
  auto authority = url.substr(separator + 3);
  authority = authority.substr(0, authority.find_first_of("/?#"));
  require(!authority.empty(), Code::InvalidConfiguration, field);
  if (const auto at = authority.rfind('@'); at != std::string_view::npos) { authority.remove_prefix(at + 1); }
  std::string_view host;
  std::string_view port;
  if (authority.starts_with('[')) {
    const auto end = authority.find(']');
    require(end != std::string_view::npos, Code::InvalidConfiguration, field);
    host = authority.substr(1, end - 1);
    (void)numericAddress(host, field);
    require(host.find(':') != std::string_view::npos, Code::InvalidConfiguration, field);
    if (end + 1 < authority.size()) {
      require(authority[end + 1] == ':', Code::InvalidConfiguration, field);
      port = authority.substr(end + 2);
      (void)portNumber(port, field);
    }
  } else {
    const auto colon = authority.find(':');
    host = authority.substr(0, colon);
    if (colon != std::string_view::npos) { port = authority.substr(colon + 1); (void)portNumber(port, field); }
    hostName(host, field);
  }
  require(kind != UrlKind::Udp || !port.empty(), Code::InvalidConfiguration, field);
  return kind;
}
} // namespace

std::expected<NetworkPolicy, NetworkPolicyError> readNetworkPolicy(
    const core::Document* configuration, NetworkPolicyOptions options) {
  try {
    require(options.mode == NetworkMode::IsolatedLoopback || options.mode == NetworkMode::Normal,
        Code::InvalidConfiguration, "mode");
    NetworkPolicy p;
    if (options.mode == NetworkMode::IsolatedLoopback) { return p; }
    const Config c(configuration);
    p.mode = NetworkMode::Normal;
    require(c.boolean("Network Selection Default.Public", true)
        && !c.boolean("Network Selection Default.Tor", false) && !c.boolean("Network Selection Default.I2P", false),
        Code::Unsupported, "Network Selection Default");
    p.ipv6 = c.boolean("IPV6 Enable Support", false);
    const bool tcp = c.boolean("peercontrol.tcp.public.enable", true);
    const bool udp = c.boolean("peercontrol.udp.public.enable", true);
    p.incomingTcp = tcp && c.boolean("TCP.Listen.Port.Enable", true)
        && c.boolean("Peer Source Selection Default.Incoming", true);
    p.outgoingTcp = tcp;
    p.incomingUtp = udp && c.boolean("UDP.Listen.Port.Enable", true)
        && c.boolean("Peer Source Selection Default.Incoming", true);
    p.outgoingUtp = udp;
    p.httpTrackers = c.boolean("Peer Source Selection Default.Tracker", true)
        && c.boolean("Tracker Client Enable TCP", true);
    p.udpTrackers = c.boolean("Peer Source Selection Default.Tracker", true)
        && c.boolean("Server Enable UDP", true);
    p.dht = c.boolean("Peer Source Selection Default.DHT", true) && c.boolean("Plugin.DHT.dht.enabled", true);
    p.pex = c.boolean("Peer Source Selection Default.PeerExchange", true);
    p.webSeeds = true;
    p.lsd = options.localDiscovery;
    p.upnp = options.portMapping && c.boolean("Plugin.UPnP.upnp.enable", true);
    p.natpmp = p.upnp && c.boolean("Plugin.UPnP.natpmp.enable", false);
    const int tcpPort = c.port("TCP.Listen.Port", 6881);
    const int udpPort = c.port("UDP.Listen.Port", 6881);
    const bool separate = !c.boolean("UDP.NonData.Listen.Port.Same", true);
    const int nonDataPort = c.port("UDP.NonData.Listen.Port", 6881);
    const bool needsUdp = p.incomingUtp || p.outgoingUtp || p.dht || p.udpTrackers;
    require(!needsUdp || !p.incomingTcp || tcpPort == udpPort, Code::Unsupported, "TCP/UDP listen ports");
    require(!separate || (!p.dht && !p.udpTrackers) || nonDataPort == udpPort,
        Code::Unsupported, "UDP.NonData.Listen.Port");
    p.listenPort = needsUdp ? udpPort : tcpPort;
    require(!c.boolean("Listen.Port.Randomize.Enable", false), Code::Unsupported, "Listen.Port.Randomize.Enable");
    require(c.text("TCP.Listen.Port.Override").empty(), Code::Unsupported, "TCP.Listen.Port.Override");
    require(!c.boolean("HTTP.Data.Listen.Port.Enable", false), Code::Unsupported, "HTTP.Data.Listen.Port.Enable");
    require(c.text("Additional Service Bind IPs").empty(), Code::Unsupported, "Additional Service Bind IPs");
    require(!c.boolean("Ignore Bind For LAN", false), Code::Unsupported, "Ignore Bind For LAN");
    p.enforceBind = c.boolean("Enforce Bind IP", false);
    p.checkBindOnStart = c.boolean("Check Bind IP On Start", true);
    p.bindAddresses.clear();
    auto bind = c.text("Bind IP");
    auto remaining = trim(bind);
    if (remaining.empty()) {
      require(!p.enforceBind, Code::Unsupported, "Enforce Bind IP");
      p.bindAddresses.emplace_back("0.0.0.0");
      if (p.ipv6) { p.bindAddresses.emplace_back("::"); }
    } else {
      while (!remaining.empty()) {
        const auto end = remaining.find(';');
        const auto selector = parseNetworkBindSelector(trim(remaining.substr(0, end)));
        if (!selector) { return std::unexpected(selector.error()); }
        const auto& address = selector->canonical;
        require(p.ipv6 || selector->family != NetworkBindFamily::V6, Code::Unsupported, "Bind IP / IPv6 disabled");
        require(p.bindAddresses.size() < 16, Code::Limit, "Bind IP");
        require(std::ranges::find(p.bindAddresses, address) == p.bindAddresses.end(), Code::InvalidConfiguration, "Bind IP");
        p.bindAddresses.push_back(address);
        if (end == std::string_view::npos) { break; }
        remaining.remove_prefix(end + 1);
        require(!trim(remaining).empty(), Code::InvalidConfiguration, "Bind IP");
      }
    }
    p.uploadBytesPerSecond = c.rate("Max Upload Speed KBs");
    p.downloadBytesPerSecond = c.rate("Max Download Speed KBs");
    p.seedingUploadBytesPerSecond = c.rate("Max Upload Speed Seeding KBs");
    p.seedingOnlyUpload = c.boolean("enable.seedingonly.upload.rate", false);
    require(!c.boolean("LAN Speed Enabled", false), Code::Unsupported, "LAN Speed Enabled");
    require(!c.boolean("Auto Upload Speed Enabled", false)
        && !c.boolean("Auto Upload Speed Seeding Enabled", false), Code::Unsupported, "Auto Upload Speed Enabled");
    p.requireEncryption = c.boolean("network.transport.encrypted.require", false);
    require(c.boolean("network.transport.encrypted.allow.incoming", true),
        Code::Unsupported, "network.transport.encrypted.allow.incoming");
    if (p.requireEncryption) {
      require(!c.boolean("network.transport.encrypted.fallback.outgoing", false)
          && !c.boolean("network.transport.encrypted.fallback.incoming", false),
          Code::Unsupported, "encryption fallback");
      require(c.text("network.transport.encrypted.min_level", "RC4") == "RC4",
          Code::Unsupported, "network.transport.encrypted.min_level");
    }
    const bool trackerProxy = c.boolean("Enable.Proxy", false);
    const bool dataProxy = c.boolean("Proxy.Data.Enable", false);
    if (trackerProxy || dataProxy) {
      const bool socks = c.boolean("Enable.SOCKS", false);
      const bool same = c.boolean("Proxy.Data.Same", true);
      NetworkProxy tracker;
      if (trackerProxy) { tracker = proxyEndpoint(c, false, true, socks ? NetworkProxyKind::Socks5 : NetworkProxyKind::Http); }
      NetworkProxy data;
      if (dataProxy) {
        const auto version = c.text("Proxy.Data.SOCKS.version", "V4");
        require(version == "V4" || version == "V5", Code::Unsupported, "Proxy.Data.SOCKS.version");
        data = proxyEndpoint(c, true, same, version == "V4" ? NetworkProxyKind::Socks4 : NetworkProxyKind::Socks5);
        require(c.text("Proxy.Data.NoProxy.PeerSet").empty(), Code::Unsupported, "Proxy.Data.NoProxy.PeerSet");
        if (!same) {
          for (int i = 2; i <= 3; ++i) {
            require(trim(c.text("Proxy.Data.Host." + std::to_string(i))).empty()
                && trim(c.text("Proxy.Data.Port." + std::to_string(i))).empty(), Code::Unsupported, "multiple data proxies");
          }
        }
      }
      require(!trackerProxy || !dataProxy || sameProxy(tracker, data), Code::Unsupported, "separate tracker/data proxies");
      p.proxy = dataProxy ? std::move(data) : std::move(tracker);
      p.proxy.peers = dataProxy;
      p.proxy.trackers = trackerProxy;
      p.proxy.remoteDns = c.boolean("Proxy.SOCKS.Tracker.DNS.Disable", true);
      // Peer-proxied sessions have no direct incoming listener in LT.
      if (dataProxy) { p.incomingTcp = false; p.incomingUtp = false; }
    }
    validate(p);
    return p;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "network policy"}); }
}

std::expected<void, NetworkPolicyError> validateNetworkPolicy(const NetworkPolicy& policy) {
  try { validate(policy); return {}; }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "network policy"}); }
}

std::expected<libtorrent::settings_pack, NetworkPolicyError> makeNetworkSettings(
    const NetworkPolicy& p, bool seedingOnly) {
  try {
    validate(p);
    Settings result;
    std::string listen, outgoing;
    for (const auto& address : p.bindAddresses) {
      const auto selected = parseNetworkBindSelector(address);
      require(selected && selected->numeric, Code::Unsupported, "Bind IP needs worker resolution");
      if (!listen.empty()) { listen += ','; outgoing += ','; }
      listen += address.find(':') == std::string::npos ? address : '[' + address + ']';
      listen += ':' + std::to_string(p.listenPort);
      outgoing += address;
    }
    result.set_str(Settings::listen_interfaces, listen);
    result.set_str(Settings::outgoing_interfaces, outgoing);
    // A requested source binding/port never silently falls back to another port.
    result.set_int(Settings::max_retry_port_bind, 0);
    result.set_bool(Settings::listen_system_port_fallback, false);
    result.set_bool(Settings::enable_incoming_tcp, p.incomingTcp);
    result.set_bool(Settings::enable_outgoing_tcp, p.outgoingTcp);
    result.set_bool(Settings::enable_incoming_utp, p.incomingUtp);
    result.set_bool(Settings::enable_outgoing_utp, p.outgoingUtp);
    result.set_bool(Settings::enable_dht, p.dht);
    result.set_bool(Settings::enable_lsd, p.lsd);
    result.set_bool(Settings::enable_upnp, p.upnp);
    result.set_bool(Settings::enable_natpmp, p.natpmp);
    result.set_str(Settings::dht_bootstrap_nodes, p.dht ? "dht.libtorrent.org:25401" : "");
    result.set_int(Settings::download_rate_limit, p.downloadBytesPerSecond);
    result.set_int(Settings::upload_rate_limit, seedingOnly && p.seedingOnlyUpload
        ? p.seedingUploadBytesPerSecond : p.uploadBytesPerSecond);
    result.set_int(Settings::out_enc_policy, p.requireEncryption ? Settings::pe_forced : Settings::pe_enabled);
    result.set_int(Settings::in_enc_policy, p.requireEncryption ? Settings::pe_forced : Settings::pe_enabled);
    result.set_int(Settings::allowed_enc_level, p.requireEncryption ? Settings::pe_rc4 : Settings::pe_both);
    const auto& proxy = p.proxy;
    int proxyType = Settings::none;
    if (proxy.kind == NetworkProxyKind::Socks4) { proxyType = Settings::socks4; }
    if (proxy.kind == NetworkProxyKind::Socks5) { proxyType = proxy.username.empty() ? Settings::socks5 : Settings::socks5_pw; }
    if (proxy.kind == NetworkProxyKind::Http) { proxyType = proxy.username.empty() ? Settings::http : Settings::http_pw; }
    result.set_int(Settings::proxy_type, proxyType);
    result.set_str(Settings::proxy_hostname, proxy.host);
    result.set_int(Settings::proxy_port, proxy.port);
    result.set_str(Settings::proxy_username, proxy.username);
    result.set_str(Settings::proxy_password, proxy.password);
    result.set_bool(Settings::proxy_peer_connections, proxy.peers);
    result.set_bool(Settings::proxy_tracker_connections, proxy.trackers);
    result.set_bool(Settings::proxy_hostnames, proxy.remoteDns);
    // Explicitly disallow inherited SAM routing; Tor/I2P need their own adapter.
    result.set_str(Settings::i2p_hostname, "");
    result.set_int(Settings::i2p_port, 0);
    result.set_bool(Settings::allow_i2p_mixed, false);
    return result;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "network settings"}); }
}

std::expected<void, NetworkPolicyError> applyTorrentNetworkPolicy(
    const NetworkPolicy& p, const TorrentNetworkSelection& selection, libtorrent::add_torrent_params& params) {
  try {
    validate(p);
    require(selection.publicNetwork && !selection.tor && !selection.i2p, Code::Unsupported, "torrent networks");
    require(params.trackers.size() <= MaximumSources && params.url_seeds.size() <= MaximumSources
        && params.http_seeds.size() <= MaximumSources && params.dht_nodes.size() <= MaximumSources
        && params.peers.size() <= MaximumSources, Code::Limit, "torrent sources");
    // Copy before editing: policy rejection and allocation failures leave caller
    // state intact. torrent_info is not cloned or mutated.
    auto next = params;
    const bool isolated = p.mode == NetworkMode::IsolatedLoopback;
    const bool privateTorrent = selection.privateTorrent || (params.ti && params.ti->priv());
    const auto previousFlags = next.flags;
    require(next.tracker_tiers.size() <= next.trackers.size(), Code::InvalidConfiguration, "tracker tiers");
    const int lastTier = next.tracker_tiers.empty() ? 0 : next.tracker_tiers.back();
    next.tracker_tiers.resize(next.trackers.size(), lastTier);
    if (params.ti) {
      require(params.ti->trackers().size() <= MaximumSources && params.ti->web_seeds().size() <= MaximumSources,
          Code::Limit, "metadata sources");
      if (!(previousFlags & libtorrent::torrent_flags::override_trackers)) {
        for (const auto& tracker : params.ti->trackers()) {
          if (std::ranges::find(next.trackers, tracker.url) == next.trackers.end()) {
            next.trackers.push_back(tracker.url);
            next.tracker_tiers.push_back(tracker.tier);
          }
        }
      }
      if (!(previousFlags & libtorrent::torrent_flags::override_web_seeds)) {
        for (const auto& seed : params.ti->web_seeds()) {
          // add_torrent_params string vectors cannot preserve these metadata
          // extensions. Reject rather than discard authentication/headers.
          require(isolated || !p.webSeeds || !selection.webSeeds || (seed.auth.empty() && seed.extra_headers.empty()),
              Code::Unsupported, "webseed authentication extensions");
          auto& seeds = seed.type == libtorrent::web_seed_entry::url_seed ? next.url_seeds : next.http_seeds;
          if (std::ranges::find(seeds, seed.url) == seeds.end()) { seeds.push_back(seed.url); }
        }
      }
    }
    require(next.trackers.size() <= MaximumSources && next.url_seeds.size() <= MaximumSources
        && next.http_seeds.size() <= MaximumSources, Code::Limit, "merged torrent sources");
    std::vector<std::string> trackers;
    std::vector<int> tiers;
    for (std::size_t i = 0; i < next.trackers.size(); ++i) {
      if (isolated) { continue; }
      const auto kind = urlKind(next.trackers[i], "tracker URL");
      if ((kind == UrlKind::Udp && !p.udpTrackers) || (kind != UrlKind::Udp && !p.httpTrackers)) { continue; }
      trackers.push_back(next.trackers[i]);
      tiers.push_back(i < next.tracker_tiers.size() ? next.tracker_tiers[i] : 0);
    }
    const auto filterSeeds = [&](auto& seeds) {
      if (isolated || !p.webSeeds || !selection.webSeeds) { seeds.clear(); return; }
      for (const auto& seed : seeds) {
        require(urlKind(seed, "webseed URL") != UrlKind::Udp, Code::Unsupported, "webseed URL");
      }
    };
    filterSeeds(next.url_seeds);
    filterSeeds(next.http_seeds);
    next.trackers = std::move(trackers);
    next.tracker_tiers = std::move(tiers);
    next.flags |= libtorrent::torrent_flags::override_trackers | libtorrent::torrent_flags::override_web_seeds;
    // Preserve any stronger existing disable bits; never broaden resume policy.
    if (isolated || privateTorrent || !p.dht || !selection.dhtBackup
        || (previousFlags & libtorrent::torrent_flags::disable_dht)) {
      next.flags |= libtorrent::torrent_flags::disable_dht;
      next.dht_nodes.clear();
    } else {
      for (const auto& [host, port] : next.dht_nodes) {
        hostName(host, "DHT node");
        require(p.ipv6 || host.find(':') == std::string::npos, Code::Unsupported, "DHT node / IPv6 disabled");
        require(port >= 1 && port <= 65535, Code::InvalidConfiguration, "DHT node port");
      }
    }
    if (isolated || privateTorrent || !p.pex) { next.flags |= libtorrent::torrent_flags::disable_pex; }
    if (isolated || privateTorrent || !p.lsd) { next.flags |= libtorrent::torrent_flags::disable_lsd; }
    for (const auto& peer : next.peers) {
      require(peer.port() != 0 && (p.ipv6 || peer.address().is_v4()), Code::Unsupported, "peer endpoint");
      if (isolated) {
        require(peer.address().is_loopback() && peer.address().is_v4() && peer.port() != 0,
            Code::Unsupported, "isolated peer endpoint");
      }
    }
    params = std::move(next);
    return {};
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(NetworkPolicyError{Code::Memory, "torrent policy"}); }
}
} // namespace biglybtc::engine
