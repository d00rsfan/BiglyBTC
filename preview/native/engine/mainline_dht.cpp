#include "mainline_dht.hpp"
#include "persistent_runtime_guard.hpp"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/kademlia/dht_state.hpp>
#include <libtorrent/socket_type.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <string_view>
#include <thread>

namespace biglybtc::engine {
namespace {
namespace lt = libtorrent;
using Code = storage::ErrorCode;
using Node = std::pair<std::string, int>;
std::unexpected<storage::Error> error(Code code, const char* message) {
  return std::unexpected(storage::Error{code, message});
}
bool available(std::uint64_t value, std::uint64_t increment = 1) {
  return increment <= std::numeric_limits<std::uint64_t>::max() - value;
}
std::expected<Node, storage::Error> node(const Node& input, bool ipv4, bool ipv6) {
  if (input.first.empty() || input.first.size() > 253 || input.second < 1 || input.second > 65535) {
    return error(Code::InvalidFormat, "Invalid Mainline DHT bootstrap endpoint");
  }
  lt::error_code ec;
  const auto address = lt::make_address(input.first, ec);
  if (!ec) {
    if (address.is_unspecified() || address.is_multicast()
        || (address.is_v4() && (!ipv4 || address.to_v4().to_uint() == 0xffffffffu))
        || (address.is_v6() && !ipv6)) {
      return error(Code::Unsupported, "Unsupported Mainline DHT bootstrap address");
    }
    return Node{address.to_string(), input.second};
  }
  std::string host = input.first;
  for (char& c : host) {
    if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
      return error(Code::InvalidFormat, "Invalid Mainline DHT bootstrap name");
    }
  }
  if (host.back() == '.') { host.pop_back(); }
  if (host.empty() || host.back() == '.' || host == "onion" || host == "i2p"
      || host == "onion.alt" || host == "i2p.alt" || host.ends_with(".onion") || host.ends_with(".i2p")
      || host.ends_with(".onion.alt") || host.ends_with(".i2p.alt")) {
    return error(Code::Unsupported, "Unsupported Mainline DHT bootstrap name");
  }
  if (std::ranges::all_of(host, [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
    return error(Code::InvalidFormat, "Invalid numeric Mainline DHT bootstrap address");
  }
  std::size_t begin = 0;
  while (begin < host.size()) {
    const auto end = host.find('.', begin);
    const auto length = (end == std::string::npos ? host.size() : end) - begin;
    if (length == 0 || length > 63 || host[begin] == '-' || host[begin + length - 1] == '-') {
      return error(Code::InvalidFormat, "Invalid Mainline DHT bootstrap name");
    }
    if (end == std::string::npos) { break; }
    begin = end + 1;
  }
  return Node{std::move(host), input.second};
}
std::string endpoint(const Node& value) {
  return (value.first.find(':') == std::string::npos ? value.first : '[' + value.first + ']')
      + ':' + std::to_string(value.second);
}
constexpr std::size_t MaximumStateBytes = MainlineDhtService::MaximumStateBytes;
constexpr std::size_t MaximumContacts = MainlineDhtService::MaximumRoutingContacts;
constexpr std::size_t MaximumIdentities = MainlineDhtService::MaximumNodeIdentities;
using Address = lt::address;
using UdpEndpoint = lt::udp::endpoint;

bool knownKeys(const core::Node& value, std::initializer_list<std::string_view> allowed) {
  if (value.type != core::Node::Type::Dictionary || value.children.size() % 2 != 0) { return false; }
  std::set<std::string_view> seen;
  for (std::size_t i = 0; i < value.children.size(); i += 2) {
    const auto& key = value.children[i];
    if (key.type != core::Node::Type::Bytes || !seen.insert(key.bytes).second
        || std::find(allowed.begin(), allowed.end(), key.bytes) == allowed.end()) { return false; }
  }
  return true;
}
bool enabled(const Address& address, bool ipv4, bool ipv6) {
  return address.is_v4() ? ipv4 : ipv6;
}
bool unicastAddress(const Address& address) {
  if (address.is_unspecified() || address.is_multicast()) { return false; }
  if (address.is_v4()) {
    const auto value = address.to_v4().to_uint();
    // Exclude this-network, multicast, reserved and limited-broadcast ranges.
    return (value >> 24) != 0 && (value >> 24) < 224;
  }
  return !address.to_v6().is_v4_mapped() && address.to_v6().scope_id() == 0;
}
bool privateAddress(const Address& address) {
  if (address.is_v4()) {
    const auto value = address.to_v4().to_uint();
    return (value & 0xff000000u) == 0x0a000000u
        || (value & 0xfff00000u) == 0xac100000u
        || (value & 0xffff0000u) == 0xc0a80000u
        || (value & 0xffc00000u) == 0x64400000u
        || (value & 0xffff0000u) == 0xa9fe0000u || address.is_loopback();
  }
  const auto value = address.to_v6();
  return value.is_loopback() || value.is_link_local() || value.is_site_local()
      || (value.to_bytes()[0] & 0xfe) == 0xfc;
}
bool matchesBinding(const Address& address, const std::vector<Address>& bindings) {
  return std::ranges::any_of(bindings, [&](const auto& bound) {
    return address.is_v4() == bound.is_v4() && (bound.is_unspecified() || bound == address);
  });
}
bool permittedContact(const UdpEndpoint& contact, bool ipv4, bool ipv6,
    const std::vector<Address>& bindings) {
  const auto& address = contact.address();
  if (contact.port() == 0 || !unicastAddress(address) || !enabled(address, ipv4, ipv6)) { return false; }
  if (!privateAddress(address)) { return true; }
  // A cache cannot invent a private route. Explicit loopback fixtures are the
  // sole supported private contact scope; wildcard bindings do not authorise it.
  return address.is_loopback() && std::ranges::any_of(bindings, [&](const auto& bound) {
    return bound.is_loopback() && bound.is_v4() == address.is_v4();
  });
}
Address compactAddress(std::string_view value) {
  if (value.size() == 4) {
    lt::address_v4::bytes_type bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) { bytes[i] = static_cast<unsigned char>(value[i]); }
    return lt::address_v4(bytes);
  }
  lt::address_v6::bytes_type bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) { bytes[i] = static_cast<unsigned char>(value[i]); }
  return lt::address_v6(bytes);
}
UdpEndpoint compactEndpoint(std::string_view value) {
  const auto size = value.size();
  const auto port = static_cast<unsigned short>((static_cast<unsigned char>(value[size - 2]) << 8)
      | static_cast<unsigned char>(value[size - 1]));
  return {compactAddress(value.substr(0, size - 2)), port};
}
bool zeroIdentity(std::string_view value) {
  return std::ranges::all_of(value.substr(0, 20), [](char byte) { return byte == 0; });
}

std::expected<lt::dht::dht_state, storage::Error> restoredState(const MainlineDhtOptions& options,
    bool ipv4, bool ipv6, const std::vector<Address>& bindings) {
  if (options.restoredState.size() > MaximumStateBytes || options.restoredNodes.size() > MainlineDhtService::MaximumNodes) {
    return error(Code::InvalidFormat, "Mainline DHT restored state exceeds its bounded format");
  }
  lt::dht::dht_state result;
  if (!options.restoredState.empty()) {
    // Validate strictly before LT's intentionally permissive state reader.
    // Unknown outer wrappers belong to the profile codec, never this parser.
    auto document = core::Document::parse(options.restoredState, {MaximumStateBytes, 256, 1024, 5});
    if (!document || !knownKeys(document->root(), {"dht state"}) || document->root().children.size() != 2) {
      return error(Code::InvalidFormat, "Invalid Mainline DHT state envelope");
    }
    const auto* state = document->find("dht state");
    if (!state || !knownKeys(*state, {"node-id", "nodes", "nodes6"})) {
      return error(Code::InvalidFormat, "Invalid Mainline DHT state fields");
    }
    std::size_t contacts = 0;
    for (const auto key : {"nodes", "nodes6"}) {
      const auto* values = state->find(key);
      if (!values) { continue; }
      if (values->type != core::Node::Type::List || values->children.size() > MaximumContacts - contacts) {
        return error(Code::InvalidFormat, "Mainline DHT routing contact limit or type");
      }
      contacts += values->children.size();
      for (const auto& value : values->children) {
        if (value.type != core::Node::Type::Bytes || (value.bytes.size() != 6 && value.bytes.size() != 18)) {
          return error(Code::InvalidFormat, "Invalid Mainline DHT compact contact");
        }
        const auto contact = compactEndpoint(value.bytes);
        if (contact.port() == 0 || !unicastAddress(contact.address())) {
          return error(Code::InvalidFormat, "Invalid Mainline DHT restored contact address or port");
        }
        // A valid cache survives a user disabling one address family. Its
        // contacts are discarded below; this is not cache corruption.
        if (enabled(contact.address(), ipv4, ipv6) && !permittedContact(contact, ipv4, ipv6, bindings)) {
          return error(Code::InvalidFormat, "Mainline DHT restored contact has an ineligible private route");
        }
      }
    }
    if (const auto* values = state->find("node-id")) {
      if (values->type != core::Node::Type::List || values->children.size() > MaximumIdentities) {
        return error(Code::InvalidFormat, "Mainline DHT node identity limit or type");
      }
      std::map<Address, std::string_view> identities;
      for (const auto& value : values->children) {
        if (value.type != core::Node::Type::Bytes || (value.bytes.size() != 24 && value.bytes.size() != 36)
            || zeroIdentity(value.bytes)) {
          return error(Code::InvalidFormat, "Invalid Mainline DHT address-specific node identity");
        }
        const auto address = compactAddress(value.bytes.substr(20));
        if (!unicastAddress(address)) { return error(Code::InvalidFormat, "Invalid Mainline DHT node identity address"); }
        const auto [at, inserted] = identities.emplace(address, value.bytes.substr(0, 20));
        if (!inserted && at->second != value.bytes.substr(0, 20)) {
          return error(Code::InvalidFormat, "Conflicting Mainline DHT identities for one address");
        }
      }
    }
    auto parameters = lt::read_session_params(lt::span<const char>(options.restoredState.data(), options.restoredState.size()),
        lt::session_handle::save_dht_state);
    result = std::move(parameters.dht_state); // Never restore settings/proxy/extensions.
  }
  // Both LT lists may contain either family (dht_tracker::state appends all
  // sockets to nodes). Rebuild them by actual address and deduplicate contacts.
  std::set<UdpEndpoint> contacts;
  for (const auto* values : {&result.nodes, &result.nodes6}) {
    for (const auto& contact : *values) {
      if (enabled(contact.address(), ipv4, ipv6)) { contacts.insert(contact); }
    }
  }
  for (const auto& hint : options.restoredNodes) {
    lt::error_code ec; const auto address = lt::make_address(hint.first, ec);
    // Legacy contacts are optional, untrusted hints. Bad or ineligible
    // contacts cannot poison an otherwise valid same-policy service startup.
    if (ec || hint.second < 1 || hint.second > 65535) { continue; }
    const UdpEndpoint contact(address, static_cast<unsigned short>(hint.second));
    if (!permittedContact(contact, ipv4, ipv6, bindings)) { continue; }
    if (contacts.size() < MaximumContacts) { contacts.insert(contact); }
  }
  if (contacts.size() > MaximumContacts) { return error(Code::InvalidFormat, "Merged Mainline DHT routing contact limit"); }
  result.nodes.clear(); result.nodes6.clear();
  for (const auto& contact : contacts) { (contact.address().is_v4() ? result.nodes : result.nodes6).push_back(contact); }
  std::erase_if(result.nids, [&](const auto& identity) {
    return !options.restoreIdentity || !enabled(identity.first, ipv4, ipv6) || !matchesBinding(identity.first, bindings);
  });
  std::set<Address> identities;
  std::erase_if(result.nids, [&](const auto& identity) { return !identities.insert(identity.first).second; });
  return result;
}

lt::dht::dht_state boundedCheckpoint(const lt::dht::dht_state& state, bool ipv4, bool ipv6,
    const std::vector<Address>& bindings) {
  lt::dht::dht_state result;
  std::set<Address> identities;
  for (const auto& identity : state.nids) {
    if (result.nids.size() == MaximumIdentities) { break; }
    if (!identity.second.is_all_zeros() && unicastAddress(identity.first) && enabled(identity.first, ipv4, ipv6)
        && matchesBinding(identity.first, bindings) && identities.insert(identity.first).second) { result.nids.push_back(identity); }
  }
  std::set<UdpEndpoint> contacts;
  for (const auto* values : {&state.nodes, &state.nodes6}) {
    for (const auto& contact : *values) {
      if (contacts.size() == MaximumContacts) { break; }
      if (permittedContact(contact, ipv4, ipv6, bindings)) { contacts.insert(contact); }
    }
  }
  for (const auto& contact : contacts) { (contact.address().is_v4() ? result.nodes : result.nodes6).push_back(contact); }
  return result;
}

} // namespace

struct MainlineDhtService::Impl {
  struct Request { bool lookup = false, lookupIssued = false; unsigned short port = 0; bool announceIssued = false; };
  const std::thread::id worker = std::this_thread::get_id();
  std::unique_ptr<lt::session> session;
  std::map<lt::sha1_hash, Request> requests;
  std::set<Node> nodes;
  std::vector<Node> pendingNodes;
  std::vector<Address> bindings;
  MainlineDhtStatus status;
  bool ipv4 = false, ipv6 = false, running = false;
  unsigned short port = 0;

  std::expected<void, storage::Error> check() const {
    if (std::this_thread::get_id() != worker) { return error(Code::Busy, "Mainline DHT requires its owner worker"); }
    if (status.phase == MainlineDhtPhase::Closed) { return error(Code::Cancelled, "Mainline DHT is closed"); }
    if (status.phase == MainlineDhtPhase::Failed) { return error(Code::Io, "Mainline DHT is unavailable"); }
    return {};
  }
  std::unexpected<storage::Error> fail(const char* message, bool lost = false) {
    status.phase = MainlineDhtPhase::Failed;
    status.listenPort = 0;
    status.observationsLost |= lost;
    requests.clear(); pendingNodes.clear(); status.activeHashes = 0;
    session.reset(); // Joins this service only; no borrowed owner or callbacks.
    return error(Code::Io, message);
  }
  std::expected<Request*, storage::Error> request(lt::sha1_hash hash) {
    if (hash.is_all_zeros()) { return error(Code::InvalidFormat, "Invalid Mainline DHT info hash"); }
    auto found = requests.find(hash);
    if (found == requests.end()) {
      if (requests.size() == MaximumActiveHashes) { return error(Code::Busy, "Mainline DHT active hash limit"); }
      found = requests.try_emplace(hash).first;
      status.activeHashes = requests.size();
    }
    return &found->second;
  }
  bool validPeer(const lt::tcp::endpoint& peer) const {
    const auto& address = peer.address();
    return peer.port() != 0 && !address.is_unspecified() && !address.is_multicast()
        && (address.is_v4() ? ipv4 && address.to_v4().to_uint() != 0xffffffffu : ipv6);
  }
};

MainlineDhtService::MainlineDhtService(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MainlineDhtService::~MainlineDhtService() = default;

std::expected<std::unique_ptr<MainlineDhtService>, storage::Error> MainlineDhtService::create(
    const NetworkPolicy& policy, const MainlineDhtOptions& options) {
  try {
    // Reject before constructing LT, including before any DNS/socket activity.
    if (policy.mode != NetworkMode::Normal || !policy.dht) {
      return error(Code::Unsupported, "Mainline DHT is not enabled by the resolved public policy");
    }
    if (policy.proxy.kind != NetworkProxyKind::None) {
      return error(Code::Unsupported, "Proxied Mainline DHT has no qualified UDP route");
    }
    if (options.port < 1 || options.port > 65535 || (!options.ipv4 && !options.ipv6)) {
      return error(Code::InvalidFormat, "Invalid Mainline DHT listener options");
    }
    if (policy.listenPort != 0 && options.port == policy.listenPort) {
      return error(Code::Conflict, "Mainline DHT and transfer ports must be distinct");
    }
    if (options.bootstrapNodes.size() > MaximumNodes) {
      return error(Code::Unsupported, "Mainline DHT bootstrap node limit");
    }
    auto valid = validateNetworkPolicy(policy);
    if (!valid) { return error(Code::Unsupported, "Mainline DHT requires a valid network policy"); }
    if (!detail::persistentRuntimeQualified()) {
      return error(Code::Unsupported, "Mainline DHT requires the source-bound runtime provider");
    }
    auto impl = std::make_unique<Impl>();
    impl->ipv4 = options.ipv4 && policy.mainlineIpv4;
    impl->ipv6 = options.ipv6 && policy.ipv6 && policy.mainlineIpv6;
    const bool peerOnly = options.onlyPeerBootstrap || policy.mainlinePeerBootstrapOnly;
    impl->port = static_cast<unsigned short>(options.port);
    auto selected = policy;
    selected.bindAddresses.clear();
    for (const auto& text : policy.bindAddresses) {
      lt::error_code ec;
      const auto address = lt::make_address(text, ec);
      if (ec) { return error(Code::Unsupported, "Mainline DHT requires resolved numeric source bindings"); }
      if ((address.is_v4() && impl->ipv4) || (address.is_v6() && impl->ipv6)) {
        selected.bindAddresses.push_back(text); impl->bindings.push_back(address);
      }
    }
    if (selected.bindAddresses.empty()) { return error(Code::Unsupported, "Mainline DHT has no enabled bound family"); }
    selected.ipv6 = impl->ipv6;
    selected.listenPort = options.port;
    selected.incomingTcp = selected.outgoingTcp = selected.incomingUtp = selected.outgoingUtp = false;
    selected.httpTrackers = selected.udpTrackers = selected.webSeeds = selected.pex = false;
    selected.lsd = selected.upnp = selected.natpmp = selected.dht = false;
    auto settings = makeNetworkSettings(selected);
    if (!settings) { return error(Code::Unsupported, "Mainline DHT network binding cannot be represented"); }
    std::set<Node> configured;
    std::string bootstrap;
    for (const auto& input : options.bootstrapNodes) {
      auto normalized = node(input, impl->ipv4, impl->ipv6);
      if (!normalized) { return std::unexpected(normalized.error()); }
      if (!configured.insert(*normalized).second) { return error(Code::Conflict, "Duplicate Mainline DHT bootstrap node"); }
      if (!peerOnly) {
        if (!bootstrap.empty()) { bootstrap += ','; }
        bootstrap += endpoint(*normalized);
      }
    }
    if (!peerOnly) { impl->nodes = std::move(configured); }
    using Settings = lt::settings_pack;
    settings->set_bool(Settings::enable_dht, true); // Only this dedicated service.
    settings->set_str(Settings::dht_bootstrap_nodes, bootstrap); // Empty stays empty.
    settings->set_bool(Settings::dht_enforce_node_id, true);
    settings->set_int(Settings::dht_max_peers_reply, static_cast<int>(MaximumPeers));
    settings->set_int(Settings::dht_max_peers, static_cast<int>(MaximumPeers));
    settings->set_int(Settings::dht_max_torrents, static_cast<int>(MaximumActiveHashes));
    settings->set_int(Settings::dht_max_dht_items, static_cast<int>(MaximumActiveHashes));
    settings->set_int(Settings::alert_queue_size, 1024);
    settings->set_int(Settings::alert_mask, static_cast<int>(lt::alert_category::status
        | lt::alert_category::error | lt::alert_category::dht_operation));
    auto restored = restoredState(options, impl->ipv4, impl->ipv6, impl->bindings);
    if (!restored) { return std::unexpected(restored.error()); }
    lt::session_params parameters(std::move(*settings));
    parameters.dht_state = std::move(*restored);
    parameters.extensions.clear(); // No default torrent plugins or hidden handles.
    impl->session = std::make_unique<lt::session>(std::move(parameters));
    return std::unique_ptr<MainlineDhtService>(new MainlineDhtService(std::move(impl)));
  } catch (const std::bad_alloc&) { return error(Code::Io, "Mainline DHT allocation failed"); }
  catch (...) { return error(Code::Io, "Mainline DHT startup failed"); }
}

std::expected<void, storage::Error> MainlineDhtService::lookup(lt::sha1_hash hash) {
  try {
    auto valid = impl_->check(); if (!valid) { return valid; }
    auto requested = impl_->request(hash); if (!requested) { return std::unexpected(requested.error()); }
    (*requested)->lookup = true;
    (*requested)->lookupIssued = false;
    return {};
  } catch (...) { return impl_->fail("Mainline DHT lookup registration failed"); }
}
std::expected<void, storage::Error> MainlineDhtService::announce(lt::sha1_hash hash, unsigned short actualPeerPort) {
  try {
    auto valid = impl_->check(); if (!valid) { return valid; }
    if (actualPeerPort == 0 || actualPeerPort == impl_->port) {
      return error(Code::InvalidFormat, "Mainline DHT requires the distinct actual transfer port");
    }
    auto requested = impl_->request(hash); if (!requested) { return std::unexpected(requested.error()); }
    if ((*requested)->port != 0 && (*requested)->port != actualPeerPort) {
      return error(Code::Conflict, "Mainline DHT hash already has a different announcement port");
    }
    (*requested)->port = actualPeerPort;
    (*requested)->announceIssued = false;
    return {};
  } catch (...) { return impl_->fail("Mainline DHT announce registration failed"); }
}
std::expected<void, storage::Error> MainlineDhtService::addNode(const Node& input) {
  try {
    auto valid = impl_->check(); if (!valid) { return valid; }
    auto normalized = node(input, impl_->ipv4, impl_->ipv6);
    if (!normalized) { return std::unexpected(normalized.error()); }
    if (impl_->nodes.contains(*normalized)) { return error(Code::Conflict, "Duplicate Mainline DHT bootstrap node"); }
    if (impl_->nodes.size() == MaximumNodes) { return error(Code::Busy, "Mainline DHT bootstrap node limit"); }
    impl_->nodes.insert(*normalized);
    impl_->pendingNodes.push_back(*normalized);
    return {};
  } catch (...) { return impl_->fail("Mainline DHT node submission failed"); }
}
std::expected<void, storage::Error> MainlineDhtService::forget(lt::sha1_hash hash) {
  auto valid = impl_->check(); if (!valid) { return valid; }
  impl_->requests.erase(hash); impl_->status.activeHashes = impl_->requests.size();
  return {};
}

std::expected<std::vector<MainlineDhtPeers>, storage::Error> MainlineDhtService::poll() {
  try {
    auto valid = impl_->check(); if (!valid) { return std::unexpected(valid.error()); }
    std::vector<lt::alert*> alerts;
    impl_->session->pop_alerts(&alerts); // This is the only consumer of this session.
    std::map<lt::sha1_hash, std::vector<lt::tcp::endpoint>> results;
    for (const auto* alert : alerts) {
      if (lt::alert_cast<lt::alerts_dropped_alert>(alert)) { return impl_->fail("Mainline DHT alerts were lost", true); }
      if (lt::alert_cast<lt::listen_failed_alert>(alert)) { return impl_->fail("Mainline DHT source binding failed"); }
      if (const auto* listening = lt::alert_cast<lt::listen_succeeded_alert>(alert)) {
        if (listening->socket_type == lt::socket_type_t::utp) {
          if (listening->port != impl_->port) { return impl_->fail("Mainline DHT listener changed port"); }
          impl_->status.listenPort = impl_->port;
          impl_->status.phase = MainlineDhtPhase::Listening;
        }
      }
      const auto* reply = lt::alert_cast<lt::dht_get_peers_reply_alert>(alert);
      if (!reply || !impl_->requests.contains(reply->info_hash)) { continue; }
      if (!available(impl_->status.replies) || reply->num_peers() < 0
          || reply->num_peers() > static_cast<int>(MaximumPeers)) {
        return impl_->fail("Mainline DHT peer result limit exceeded", true);
      }
      ++impl_->status.replies;
      auto& peers = results[reply->info_hash];
      for (const auto& peer : reply->peers()) {
        if (!impl_->validPeer(peer) || std::ranges::find(peers, peer) != peers.end()) { continue; }
        if (peers.size() == MaximumPeers) { return impl_->fail("Mainline DHT peer result limit exceeded", true); }
        peers.push_back(peer);
      }
    }
    // Bootstrap DNS can delay LT's DHT construction; requests before it would
    // otherwise be silently ignored. Query only this session's startup barrier.
    if (!impl_->running) { impl_->running = impl_->session->is_dht_running(); }
    if (impl_->running) {
      for (const auto& value : impl_->pendingNodes) { impl_->session->add_dht_node(value); }
      impl_->pendingNodes.clear();
      for (auto& [hash, request] : impl_->requests) {
        if (request.lookup && !request.lookupIssued) {
          if (!available(impl_->status.lookups)) { return impl_->fail("Mainline DHT request counter overflow", true); }
          impl_->session->dht_get_peers(hash); request.lookupIssued = true; ++impl_->status.lookups;
        }
        if (request.port != 0 && !request.announceIssued) {
          if (!available(impl_->status.announces)) { return impl_->fail("Mainline DHT request counter overflow", true); }
          impl_->session->dht_announce(hash, request.port); request.announceIssued = true; ++impl_->status.announces;
        }
      }
    }
    std::vector<MainlineDhtPeers> output;
    output.reserve(results.size());
    for (auto& [hash, peers] : results) {
      if (!available(impl_->status.deliveredPeers, peers.size())) { return impl_->fail("Mainline DHT peer counter overflow", true); }
      impl_->status.deliveredPeers += peers.size();
      output.push_back({hash, std::move(peers)});
    }
    return output;
  } catch (...) { return impl_->fail("Mainline DHT polling failed", true); }
}
std::expected<std::string, storage::Error> MainlineDhtService::checkpoint() {
  try {
    auto valid = impl_->check(); if (!valid) { return std::unexpected(valid.error()); }
    if (!impl_->running || impl_->status.phase != MainlineDhtPhase::Listening) {
      return error(Code::Busy, "Mainline DHT checkpoint requires its running bound listener");
    }
    auto parameters = impl_->session->session_state(lt::session_handle::save_dht_state);
    parameters.dht_state = boundedCheckpoint(parameters.dht_state, impl_->ipv4, impl_->ipv6, impl_->bindings);
    const auto bytes = lt::write_session_params_buf(parameters, lt::session_handle::save_dht_state);
    if (bytes.size() > MaximumStateBytes) { return error(Code::Io, "Mainline DHT checkpoint exceeded its bounded format"); }
    return std::string(bytes.begin(), bytes.end());
  } catch (...) { return error(Code::Io, "Mainline DHT checkpoint failed"); }
}

std::expected<void, storage::Error> MainlineDhtService::close() {
  if (std::this_thread::get_id() != impl_->worker) { return error(Code::Busy, "Mainline DHT requires its owner worker"); }
  if (impl_->status.phase == MainlineDhtPhase::Closed) { return {}; }
  impl_->session.reset(); // Actual network thread retirement precedes success.
  impl_->requests.clear(); impl_->nodes.clear(); impl_->pendingNodes.clear();
  impl_->status.activeHashes = 0; impl_->status.listenPort = 0; impl_->status.phase = MainlineDhtPhase::Closed;
  return {};
}
MainlineDhtStatus MainlineDhtService::status() const noexcept {
  if (std::this_thread::get_id() != impl_->worker) {
    MainlineDhtStatus unavailable; unavailable.phase = MainlineDhtPhase::Failed; unavailable.observationsLost = true;
    return unavailable;
  }
  return impl_->status;
}

} // namespace biglybtc::engine
