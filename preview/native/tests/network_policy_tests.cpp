#include "../engine/network_policy.hpp"

#include <libtorrent/address.hpp>
#include <libtorrent/torrent_info.hpp>

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Source-derived literal config/metadata only. No session, DNS, sockets, file IO,
// original Java execution or live route/proxy-health qualification.
namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using Code = engine::NetworkPolicyErrorCode;
using Settings = lt::settings_pack;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class Function> void run(std::string_view name, Function&& function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
core::Document config(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string encoded = "d";
  for (const auto& [key, value] : fields) { encoded += bytes(key) + value; }
  auto result = core::Document::parse(encoded + 'e');
  CHECK(result);
  return std::move(*result);
}
engine::NetworkPolicy normal(const core::Document* document = nullptr,
    bool lsd = false, bool mappings = false) {
  auto result = engine::readNetworkPolicy(document, {engine::NetworkMode::Normal, lsd, mappings});
  CHECK(result);
  return std::move(*result);
}
void rejected(const core::Document& document, Code expected) {
  const auto result = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal});
  CHECK(!result && result.error().code == expected);
}
std::shared_ptr<lt::torrent_info> metadata(bool privateTorrent = false) {
  std::string encoded = "d4:infod6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:" + std::string(20, 'h');
  if (privateTorrent) { encoded += "7:privatei1e"; }
  encoded += "ee";
  lt::error_code error;
  auto result = std::make_shared<lt::torrent_info>(encoded.data(), static_cast<int>(encoded.size()), error);
  CHECK(!error && result->is_valid());
  return result;
}
} // namespace

int main() {
  try {
    run("saved decentralized marker never becomes a tracker or enables discovery", [] {
      const std::string url="dht://0123456789ABCDEF0123456789ABCDEF01234567.dht/announce";
      const std::string source="d8:announce"+bytes(url)+"4:infod6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:"+std::string(20,'h')+"ee";
      lt::error_code error;auto parsed=std::make_shared<lt::torrent_info>(source.data(),static_cast<int>(source.size()),error);
      CHECK(!error&&parsed->is_valid()&&parsed->trackers().empty());
      auto disabled=config({{"Plugin.DHT.dht.enabled",integer(0)}});const auto policy=normal(&disabled);
      lt::add_torrent_params params;params.ti=parsed;params.flags|=lt::torrent_flags::disable_pex|lt::torrent_flags::disable_lsd;
      CHECK(engine::applyTorrentNetworkPolicy(policy,{},params));CHECK(params.trackers.empty());
      CHECK((params.flags & lt::torrent_flags::disable_dht)&&(params.flags & lt::torrent_flags::disable_pex)&&(params.flags & lt::torrent_flags::disable_lsd));
      const auto isolated=engine::readNetworkPolicy(nullptr);CHECK(isolated);
      lt::add_torrent_params isolatedParams;isolatedParams.ti=parsed;
      CHECK(engine::applyTorrentNetworkPolicy(*isolated,{},isolatedParams));CHECK(isolatedParams.trackers.empty());
      CHECK(isolatedParams.flags & lt::torrent_flags::disable_dht);CHECK(isolatedParams.flags & lt::torrent_flags::disable_pex);CHECK(isolatedParams.flags & lt::torrent_flags::disable_lsd);
      auto privateInfo=metadata(true);lt::add_torrent_params privateParams;privateParams.ti=privateInfo;
      CHECK(engine::applyTorrentNetworkPolicy(normal(),{},privateParams));CHECK(privateParams.flags & lt::torrent_flags::disable_dht);
      CHECK(privateParams.flags & lt::torrent_flags::disable_pex);CHECK(privateParams.flags & lt::torrent_flags::disable_lsd);
    });
    run("pinned defaults and explicit public permission", [] {
      const auto isolated = engine::readNetworkPolicy(nullptr);
      CHECK(isolated && isolated->mode == engine::NetworkMode::IsolatedLoopback);
      const auto p = normal();
      CHECK(p.listenPort == 6881 && !p.ipv6 && p.bindAddresses == std::vector<std::string>{"0.0.0.0"});
      CHECK(p.incomingTcp && p.outgoingTcp && p.incomingUtp && p.outgoingUtp);
      CHECK(p.httpTrackers && p.udpTrackers && p.dht && p.pex && p.webSeeds);
      CHECK(!p.lsd && !p.upnp && !p.natpmp);
      const auto settings = engine::makeNetworkSettings(p);
      CHECK(settings && settings->get_str(Settings::listen_interfaces) == "0.0.0.0:6881");
      CHECK(settings->get_str(Settings::outgoing_interfaces).empty());
      CHECK(settings->get_bool(Settings::enable_dht));
      CHECK(settings->get_str(Settings::dht_bootstrap_nodes) == "dht.libtorrent.org:25401");
      CHECK(settings->get_int(Settings::proxy_type) == Settings::none);
      CHECK(settings->get_int(Settings::upload_rate_limit) == 0);
      CHECK(settings->get_int(Settings::max_retry_port_bind) == 0 && !settings->get_bool(Settings::listen_system_port_fallback));
    });
    run("unknown config/raw order unchanged", [] {
      auto document = config({{"opaque", "d3:raw3:\r\nXe"}, {"Max Upload Speed KBs", integer(17)}});
      const auto original = document.originalBytes();
      const auto p = normal(&document);
      CHECK(p.uploadBytesPerSecond == 17408);
      CHECK(document.serialize() == original && document.originalBytes() == original);
      CHECK(document.find("opaque")->type == core::Node::Type::Dictionary);
    });
    run("numeric dual-stack and strict binding", [] {
      auto document = config({{"Bind IP", bytes(" 127.0.0.1 ; 2001:0DB8::1 ")}, {"IPV6 Enable Support", integer(1)}});
      const auto p = normal(&document);
      const auto settings = engine::makeNetworkSettings(p);
      CHECK(settings && settings->get_str(Settings::listen_interfaces) == "127.0.0.1:6881,[2001:db8::1]:6881");
      CHECK(settings->get_str(Settings::outgoing_interfaces) == "127.0.0.1,2001:db8::1");
      auto interface = config({{"Bind IP", bytes("eth0[0]")}});
      const auto unresolved = normal(&interface);
      CHECK(unresolved.bindAddresses == std::vector<std::string>{"eth0[0]"});
      CHECK(engine::validateNetworkPolicy(unresolved));
      CHECK(!engine::makeNetworkSettings(unresolved)); // Worker resolution is mandatory.
      auto missing = config({{"Enforce Bind IP", integer(1)}});
      rejected(missing, Code::Unsupported);
      auto disabled = config({{"Bind IP", bytes("::1")}});
      rejected(disabled, Code::Unsupported);
      auto duplicate = config({{"Bind IP", bytes("127.0.0.1;127.0.0.1")}});
      rejected(duplicate, Code::InvalidConfiguration);
    });
    run("wildcard listen is not an outgoing literal interface restriction", [] {
      for (const auto& bind : {std::string{}, std::string{"   "}, std::string{"0.0.0.0"}}) {
        auto document = config({{"Bind IP", bytes(bind)}}); const auto original = document.originalBytes();
        const auto policy = normal(&document); const auto settings = engine::makeNetworkSettings(policy);
        CHECK(settings && settings->get_str(Settings::listen_interfaces) == "0.0.0.0:6881");
        CHECK(settings->get_str(Settings::outgoing_interfaces).empty());
        CHECK(document.serialize() == original);
      }
      auto dual = config({{"IPV6 Enable Support", integer(1)}});
      const auto dualSettings = engine::makeNetworkSettings(normal(&dual));
      CHECK(dualSettings && dualSettings->get_str(Settings::listen_interfaces) == "0.0.0.0:6881,[::]:6881"
          && dualSettings->get_str(Settings::outgoing_interfaces).empty());
      auto bothAny = config({{"IPV6 Enable Support", integer(1)}, {"Bind IP", bytes("0.0.0.0;::")}});
      const auto bothSettings = engine::makeNetworkSettings(normal(&bothAny));
      CHECK(bothSettings && bothSettings->get_str(Settings::outgoing_interfaces).empty());
      auto strict = config({{"Bind IP", bytes("127.0.0.1;::1")}, {"IPV6 Enable Support", integer(1)}, {"Enforce Bind IP", integer(1)}});
      const auto strictSettings = engine::makeNetworkSettings(normal(&strict));
      CHECK(strictSettings && strictSettings->get_str(Settings::outgoing_interfaces) == "127.0.0.1,::1");
      CHECK(strictSettings->get_str(Settings::listen_interfaces) == "127.0.0.1:6881,[::1]:6881");
      for (const auto& bind : {"0.0.0.0;127.0.0.1", "0.0.0.0;::1", "::;127.0.0.1"}) {
        auto mixed = config({{"Bind IP", bytes(bind)}, {"IPV6 Enable Support", integer(1)}}); const auto original = mixed.originalBytes();
        const auto rejected = engine::readNetworkPolicy(&mixed, {engine::NetworkMode::Normal});
        CHECK(!rejected && rejected.error().code == Code::Unsupported && rejected.error().field == "mixed wildcard and explicit Bind IP");
        CHECK(mixed.serialize() == original);
      }
      auto enforced = config({{"Bind IP", bytes("0.0.0.0")}, {"Enforce Bind IP", integer(1)}});
      CHECK(!engine::readNetworkPolicy(&enforced, {engine::NetworkMode::Normal}));
    });
    run("independent transport switches and unsupported split listeners", [] {
      auto document = config({{"TCP.Listen.Port.Enable", integer(0)}, {"peercontrol.udp.public.enable", integer(0)}});
      const auto settings = engine::makeNetworkSettings(normal(&document));
      CHECK(settings && !settings->get_bool(Settings::enable_incoming_tcp));
      CHECK(settings->get_bool(Settings::enable_outgoing_tcp));
      CHECK(!settings->get_bool(Settings::enable_incoming_utp) && !settings->get_bool(Settings::enable_outgoing_utp));
      auto split = config({{"TCP.Listen.Port", integer(5001)}, {"UDP.Listen.Port", integer(5002)}});
      rejected(split, Code::Unsupported);
      auto nonData = config({{"UDP.NonData.Listen.Port.Same", integer(0)}, {"UDP.NonData.Listen.Port", integer(5002)}});
      rejected(nonData, Code::Unsupported);
      auto invalidPort = config({{"TCP.Listen.Port", integer(65536)}});
      rejected(invalidPort, Code::InvalidConfiguration);
    });
    run("discovery requires caller permission", [] {
      CHECK(normal(nullptr, true, true).lsd && normal(nullptr, true, true).upnp);
      auto disabled = config({{"Plugin.UPnP.upnp.enable", integer(0)}, {"Plugin.DHT.dht.enabled", integer(0)}});
      const auto p = normal(&disabled, true, true);
      CHECK(!p.upnp && !p.natpmp && !p.dht);
      auto natpmp = config({{"Plugin.UPnP.natpmp.enable", integer(1)}});
      CHECK(normal(&natpmp, false, true).natpmp);
    });
    run("KiB rates and whole-session seeding switch", [] {
      auto document = config({{"Max Upload Speed KBs", integer(23)}, {"Max Download Speed KBs", integer(43)},
          {"Max Upload Speed Seeding KBs", integer(7)}, {"enable.seedingonly.upload.rate", integer(1)}});
      const auto p = normal(&document);
      CHECK(engine::makeNetworkSettings(p)->get_int(Settings::upload_rate_limit) == 23552);
      CHECK(engine::makeNetworkSettings(p, true)->get_int(Settings::upload_rate_limit) == 7168);
      CHECK(engine::makeNetworkSettings(p)->get_int(Settings::download_rate_limit) == 44032);
      auto negative = config({{"Max Upload Speed KBs", integer(-1)}});
      CHECK(normal(&negative).uploadBytesPerSecond == 0);
      auto excessive = config({{"Max Upload Speed KBs", integer(std::numeric_limits<std::int64_t>::max())}});
      rejected(excessive, Code::Limit);
      auto autospeed = config({{"Auto Upload Speed Enabled", integer(1)}});
      rejected(autospeed, Code::Unsupported);
    });
    run("SOCKS5 authentication and remote DNS", [] {
      auto document = config({{"Enable.Proxy", integer(1)}, {"Enable.SOCKS", integer(1)},
          {"Proxy.Data.Enable", integer(1)}, {"Proxy.Data.SOCKS.version", bytes("V5")},
          {"Proxy.Host", bytes("proxy.example.invalid")}, {"Proxy.Port", bytes(" +1080 ")},
          {"Proxy.Username", bytes("alice")}, {"Proxy.Password", bytes("literal-secret")}});
      const auto settings = engine::makeNetworkSettings(normal(&document));
      CHECK(settings && settings->get_int(Settings::proxy_type) == Settings::socks5_pw);
      CHECK(settings->get_str(Settings::proxy_hostname) == "proxy.example.invalid");
      CHECK(settings->get_int(Settings::proxy_port) == 1080);
      CHECK(settings->get_str(Settings::proxy_username) == "alice" && settings->get_str(Settings::proxy_password) == "literal-secret");
      CHECK(settings->get_bool(Settings::proxy_hostnames) && settings->get_bool(Settings::proxy_peer_connections)
          && settings->get_bool(Settings::proxy_tracker_connections));
      CHECK(!settings->get_bool(Settings::enable_incoming_tcp));
    });
    run("SOCKS4 configured USERID cannot be silently ignored", [] {
      auto document = config({{"Proxy.Data.Enable", integer(1)}, {"Proxy.Data.SOCKS.version", bytes("V4")},
          {"Proxy.Host", bytes("127.0.0.1")}, {"Proxy.Port", bytes("1080")},
          {"Proxy.Username", bytes("<none>")}, {"peercontrol.udp.public.enable", integer(0)},
          {"Server Enable UDP", integer(0)}, {"Peer Source Selection Default.DHT", integer(0)}});
      const auto original = document.originalBytes();
      auto policy = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal});
      CHECK(policy && policy->proxy.kind == engine::NetworkProxyKind::Socks4 && policy->proxy.username.empty());
      CHECK(!policy->proxy.remoteDns && !policy->incomingTcp && !policy->incomingUtp);
      const auto settings = engine::makeNetworkSettings(*policy);
      CHECK(settings && settings->get_int(Settings::proxy_type) == Settings::socks4
          && settings->get_str(Settings::proxy_username).empty() && !settings->get_bool(Settings::proxy_hostnames));
      CHECK(document.serialize() == original);
      CHECK(document.replaceValue("Proxy.Username", bytes("literal-private-user")));
      const auto rejectedPolicy = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal});
      CHECK(!rejectedPolicy && rejectedPolicy.error().code == Code::Unsupported
          && rejectedPolicy.error().field == "SOCKS4 USERID");
      policy->proxy.username = "literal-private-user";
      CHECK(!engine::validateNetworkPolicy(*policy) && !engine::makeNetworkSettings(*policy));
      CHECK(rejectedPolicy.error().field.find("literal-private-user") == std::string::npos);
      CHECK(document.replaceValue("Proxy.Data.SOCKS.version", bytes("V4a")));
      CHECK(!engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal}));
    });
    run("SOCKS4a and USERID require retained runtime proof, not enum or header", [] {
      auto document = config({{"Proxy.Data.Enable", integer(1)}, {"Proxy.Data.SOCKS.version", bytes("V4a")},
          {"Proxy.Host", bytes("127.0.0.1")}, {"Proxy.Port", bytes("1080")},
          {"peercontrol.udp.public.enable", integer(0)}, {"Server Enable UDP", integer(0)},
          {"Peer Source Selection Default.DHT", integer(0)}});
      const auto original = document.originalBytes();
      const auto parsed = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal});
      CHECK(!parsed && parsed.error().code == Code::Unsupported && parsed.error().field == "SOCKS4a provider");
      CHECK(document.serialize() == original);
      CHECK(document.replaceValue("Proxy.Data.SOCKS.version", bytes("V4")));
      auto numeric = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal}); CHECK(numeric);
      numeric->proxy.kind = engine::NetworkProxyKind::Socks4a; numeric->proxy.remoteDns = true;
      CHECK(!engine::validateNetworkPolicy(*numeric) && !engine::makeNetworkSettings(*numeric));
      numeric->proxy.kind = engine::NetworkProxyKind::Socks4; numeric->proxy.remoteDns = false;
      numeric->proxy.username = "owned-user";
      CHECK(!engine::validateNetworkPolicy(*numeric) && !engine::makeNetworkSettings(*numeric));
      numeric->proxy.username.clear(); CHECK(engine::validateNetworkPolicy(*numeric));
      const auto plain = engine::makeNetworkSettings(*numeric);
      CHECK(plain && plain->get_str(Settings::proxy_username).empty());
    });
    run("tracker-only HTTP proxy retains explicitly direct peers", [] {
      auto document = config({{"Enable.Proxy", integer(1)}, {"Proxy.Host", bytes("127.0.0.1")},
          {"Proxy.Port", bytes("8080")}, {"Proxy.Username", bytes(" <NoNe> ")},
          {"Server Enable UDP", integer(0)}, {"Peer Source Selection Default.DHT", integer(0)}});
      const auto settings = engine::makeNetworkSettings(normal(&document));
      CHECK(settings && settings->get_int(Settings::proxy_type) == Settings::http);
      CHECK(settings->get_str(Settings::proxy_username).empty());
      CHECK(settings->get_bool(Settings::proxy_tracker_connections) && !settings->get_bool(Settings::proxy_peer_connections));
      CHECK(settings->get_bool(Settings::enable_outgoing_utp)); // Direct data path is explicitly configured.
      CHECK(settings->get_bool(Settings::proxy_hostnames));
    });
    run("unsupported proxy routes fail without secrets", [] {
      auto httpUdp = config({{"Enable.Proxy", integer(1)}, {"Proxy.Host", bytes("proxy.invalid")},
          {"Proxy.Port", bytes("80")}, {"Proxy.Password", bytes("sensitive")}, {"Proxy.Username", bytes("u")}});
      const auto failed = engine::readNetworkPolicy(&httpUdp, {engine::NetworkMode::Normal});
      CHECK(!failed && failed.error().code == Code::Unsupported);
      CHECK(failed.error().field.find("sensitive") == std::string::npos && failed.error().field.find("proxy.invalid") == std::string::npos);
      auto split = config({{"Enable.Proxy", integer(1)}, {"Enable.SOCKS", integer(1)},
          {"Proxy.Host", bytes("first.invalid")}, {"Proxy.Port", bytes("1080")},
          {"Proxy.Data.Enable", integer(1)}, {"Proxy.Data.Same", integer(0)},
          {"Proxy.Data.Host", bytes("second.invalid")}, {"Proxy.Data.Port", bytes("1081")},
          {"Proxy.Data.SOCKS.version", bytes("V5")}});
      rejected(split, Code::Unsupported);
      auto torProxy = config({{"Enable.Proxy", integer(1)}, {"Proxy.Host", bytes("host.onion")}, {"Proxy.Port", bytes("80")}});
      rejected(torProxy, Code::Unsupported);
    });
    run("malformed known config and network selection", [] {
      auto boolean = config({{"IPV6 Enable Support", bytes("true")}});
      rejected(boolean, Code::InvalidConfiguration);
      const auto duplicate = core::Document::parse("d20:Max Upload Speed KBsi1e20:Max Upload Speed KBsi2ee");
      CHECK(!duplicate && duplicate.error().code == "duplicate_key");
      auto tor = config({{"Network Selection Default.Tor", integer(1)}});
      rejected(tor, Code::Unsupported);
      auto nonPublic = config({{"Network Selection Default.Public", integer(0)}});
      rejected(nonPublic, Code::Unsupported);
      auto root = core::Document::parse("li1ee");
      CHECK(root);
      rejected(*root, Code::InvalidConfiguration);
    });
    run("required encryption never silently falls back", [] {
      auto document = config({{"network.transport.encrypted.require", integer(1)}});
      const auto settings = engine::makeNetworkSettings(normal(&document));
      CHECK(settings && settings->get_int(Settings::out_enc_policy) == Settings::pe_forced);
      CHECK(settings->get_int(Settings::in_enc_policy) == Settings::pe_forced);
      CHECK(settings->get_int(Settings::allowed_enc_level) == Settings::pe_rc4);
      auto fallback = config({{"network.transport.encrypted.require", integer(1)},
          {"network.transport.encrypted.fallback.outgoing", integer(1)}});
      rejected(fallback, Code::Unsupported);
    });
    run("metadata tracker scheme filtering and tier preservation", [] {
      auto document = config({{"Server Enable UDP", integer(0)}});
      const auto p = normal(&document);
      lt::add_torrent_params params;
      params.ti = metadata();
      params.ti->add_tracker("https://tracker.invalid/a", 3);
      params.ti->add_tracker("udp://tracker.invalid:80/a", 4);
      params.trackers = {"http://other.invalid/a", "https://third.invalid/a"};
      params.tracker_tiers = {7}; // LT repeats the last specified tier.
      const auto originalMetadata = params.ti;
      params.save_path = "/unchanged";
      CHECK(engine::applyTorrentNetworkPolicy(p, {}, params));
      CHECK(params.trackers.size() == 3 && params.tracker_tiers.size() == 3);
      CHECK(params.tracker_tiers[0] == 7 && params.tracker_tiers[1] == 7 && params.tracker_tiers[2] == 3);
      CHECK(params.ti == originalMetadata && params.ti->trackers().size() == 2 && params.save_path == "/unchanged");
      CHECK(params.flags & lt::torrent_flags::override_trackers);
      CHECK(params.flags & lt::torrent_flags::override_web_seeds);
    });
    run("IPv6 literal sources require the original IPv6 permission", [] {
      const auto ipv4 = normal();
      for (bool tracker : {false, true}) {
        lt::add_torrent_params params;
        auto& urls = tracker ? params.trackers : params.url_seeds;
        urls = {"http://[::1]:8080/data"};
        const auto beforeFlags = params.flags;
        const auto denied = engine::applyTorrentNetworkPolicy(ipv4, {}, params);
        CHECK(!denied && denied.error().code == Code::Unsupported);
        CHECK(urls == std::vector<std::string>{"http://[::1]:8080/data"} && params.flags == beforeFlags);
        auto ipv6 = ipv4; ipv6.ipv6 = true;
        CHECK(engine::applyTorrentNetworkPolicy(ipv6, {}, params));
        CHECK(urls.size() == 1);
      }
    });
    run("private metadata and stronger resume restrictions", [] {
      auto p = normal(nullptr, true);
      lt::add_torrent_params params;
      params.ti = metadata(true);
      params.dht_nodes.emplace_back("router.invalid", 6881);
      params.flags |= lt::torrent_flags::sequential_download;
      CHECK(engine::applyTorrentNetworkPolicy(p, {}, params));
      CHECK(params.flags & lt::torrent_flags::disable_dht);
      CHECK(params.flags & lt::torrent_flags::disable_pex);
      CHECK(params.flags & lt::torrent_flags::disable_lsd);
      CHECK(params.flags & lt::torrent_flags::sequential_download);
      CHECK(params.dht_nodes.empty());
      lt::add_torrent_params restricted;
      restricted.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex;
      restricted.dht_nodes.emplace_back("router.invalid", 6881);
      CHECK(engine::applyTorrentNetworkPolicy(p, {}, restricted));
      CHECK(restricted.dht_nodes.empty() && (restricted.flags & lt::torrent_flags::disable_pex));
    });
    run("webseed filters and explicit empty resume overrides", [] {
      lt::add_torrent_params params;
      params.ti = metadata();
      params.ti->add_url_seed("https://seed.invalid/data");
      CHECK(engine::applyTorrentNetworkPolicy(normal(), {}, params));
      CHECK(params.url_seeds.size() == 1 && params.url_seeds[0] == "https://seed.invalid/data");
      lt::add_torrent_params removed;
      removed.ti = params.ti;
      removed.flags |= lt::torrent_flags::override_web_seeds;
      CHECK(engine::applyTorrentNetworkPolicy(normal(), {}, removed));
      CHECK(removed.url_seeds.empty());
      engine::TorrentNetworkSelection noSeeds;
      noSeeds.webSeeds = false;
      CHECK(engine::applyTorrentNetworkPolicy(normal(), noSeeds, params));
      CHECK(params.url_seeds.empty());
    });
    run("no direct fallback for alternate-network endpoints or selection", [] {
      for (const auto* endpoint : {"https://secret.ONION./a", "udp://secret.i2p:80/a",
          "http://secret.onion.alt/a", "https://%73ecret.onion/a", "wss://tracker.invalid/a"}) {
        lt::add_torrent_params params;
        params.trackers = {"https://ok.invalid/a", endpoint};
        params.tracker_tiers = {1, 2};
        params.save_path = "unchanged";
        const auto oldFlags = params.flags;
        const auto result = engine::applyTorrentNetworkPolicy(normal(), {}, params);
        CHECK(!result && params.trackers.size() == 2 && params.trackers[1] == endpoint);
        CHECK(params.tracker_tiers[1] == 2 && params.save_path == "unchanged" && params.flags == oldFlags);
      }
      lt::add_torrent_params params;
      engine::TorrentNetworkSelection i2p;
      i2p.i2p = true;
      CHECK(!engine::applyTorrentNetworkPolicy(normal(), i2p, params));
    });
    run("isolation ignores hostile config and strips external sources", [] {
      auto document = config({{"Enable.Proxy", integer(1)}, {"Proxy.Host", bytes("bad.onion")},
          {"Bind IP", bytes("0.0.0.0")}, {"Plugin.DHT.dht.enabled", integer(1)}});
      const auto p = engine::readNetworkPolicy(&document);
      CHECK(p);
      const auto settings = engine::makeNetworkSettings(*p);
      CHECK(settings && settings->get_str(Settings::listen_interfaces) == "127.0.0.1:0");
      CHECK(settings->get_str(Settings::outgoing_interfaces) == "127.0.0.1");
      CHECK(settings->get_str(Settings::dht_bootstrap_nodes).empty());
      CHECK(!settings->get_bool(Settings::enable_dht) && !settings->get_bool(Settings::enable_lsd));
      CHECK(!settings->get_bool(Settings::enable_upnp) && !settings->get_bool(Settings::enable_natpmp));
      CHECK(!settings->get_bool(Settings::enable_outgoing_utp) && !settings->get_bool(Settings::enable_incoming_utp));
      CHECK(settings->get_int(Settings::proxy_type) == Settings::none);
      lt::add_torrent_params params;
      params.trackers = {"udp://external.invalid:80/a"};
      params.url_seeds = {"https://external.invalid/data"};
      params.dht_nodes.emplace_back("external.invalid", 6881);
      params.peers.emplace_back(lt::make_address("127.0.0.1"), 12345);
      CHECK(engine::applyTorrentNetworkPolicy(*p, {}, params));
      CHECK(params.trackers.empty() && params.url_seeds.empty() && params.dht_nodes.empty());
      CHECK(params.peers.size() == 1);
      params.peers.emplace_back(lt::make_address("192.0.2.1"), 12345);
      CHECK(!engine::applyTorrentNetworkPolicy(*p, {}, params));
      CHECK(params.peers.size() == 2);
    });
    run("untrusted scalar values are revalidated at translation", [] {
      auto p = normal();
      p.proxy.kind = static_cast<engine::NetworkProxyKind>(99);
      CHECK(!engine::makeNetworkSettings(p));
      p = *engine::readNetworkPolicy(nullptr);
      p.dht = true;
      CHECK(!engine::makeNetworkSettings(p));
      auto huge = normal();
      lt::add_torrent_params params;
      params.trackers.resize(2049, "https://tracker.invalid/a");
      const auto result = engine::applyTorrentNetworkPolicy(huge, {}, params);
      CHECK(!result && result.error().code == Code::Limit && params.trackers.size() == 2049);
    });
    std::cout << families << " network-policy families, " << assertions << " assertions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
