#include "../engine/network_policy.hpp"
#include "../engine/network_peer_classes.hpp"
#include "../engine/live_settings.hpp"

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

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--classify-lan") {
      CHECK(argc > 2 && argc <= 258);
      const auto filter = engine::makeNetworkPeerClassFilter(normal()); CHECK(filter);
      constexpr unsigned local = 1U << static_cast<unsigned>(lt::session_handle::local_peer_class_id);
      for (int i = 2; i < argc; ++i) {
        const auto address = lt::make_address(argv[i]);
        std::cout << argv[i] << '\t' << ((filter->access(address) & local) ? 1 : 0)
            << '\t' << engine::javaProxyNumericAddress(address) << '\n';
      }
      return 0;
    }

    run("saved decentralized marker never becomes a tracker or enables discovery", [] {
      const std::string url="dht://0123456789ABCDEF0123456789ABCDEF01234567.dht/announce";
      const std::string source="d8:announce"+bytes(url)+"4:infod6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:"+std::string(20,'h')+"ee";
      lt::error_code error;auto parsed=std::make_shared<lt::torrent_info>(source.data(),static_cast<int>(source.size()),error);
      CHECK(!error&&parsed->is_valid()&&parsed->trackers().empty());
      auto disabled=config({{"Plugin.mldht.enable",integer(0)}});const auto policy=normal(&disabled);
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
      CHECK(p.mainlinePort == 49001 && p.mainlineIpv4 && !p.mainlineIpv6);
      CHECK(!p.mainlineBackupOnly && !p.mainlinePeerBootstrapOnly);
      CHECK(!settings->get_bool(Settings::enable_dht));
      CHECK(settings->get_str(Settings::dht_bootstrap_nodes).empty());
      CHECK(settings->get_int(Settings::proxy_type) == Settings::none);
      CHECK(settings->get_int(Settings::upload_rate_limit) == 0);
      CHECK(settings->get_int(Settings::max_retry_port_bind) == 0 && !settings->get_bool(Settings::listen_system_port_fallback));
    });
    run("Mainline and Azureus have independent enable switches", [] {
      auto azureusOff = config({{"Plugin.DHT.dht.enabled", integer(0)}});
      CHECK(normal(&azureusOff).dht);
      for (const auto key : {"PluginInfo.mldht.enabled", "Plugin.mldht.enable", "Peer Source Selection Default.DHT"}) {
        auto document = config({{key, integer(0)}, {"Plugin.DHT.dht.enabled", integer(1)}});
        const auto original = document.originalBytes();
        const auto policy = normal(&document);
        CHECK(!policy.dht && policy.httpTrackers && policy.outgoingTcp);
        const auto settings = engine::makeNetworkSettings(policy);
        CHECK(settings && !settings->get_bool(Settings::enable_dht)
            && settings->get_str(Settings::dht_bootstrap_nodes).empty());
        CHECK(document.serialize() == original && document.originalBytes() == original);
      }
      auto javaZero = config({{"Plugin.mldht.enable", integer(4294967296LL)}});
      CHECK(!normal(&javaZero).dht); // Original Number.intValue() projection.
      auto javaNonzero = config({{"Plugin.mldht.enable", integer(2)}});
      CHECK(normal(&javaNonzero).dht);
    });
    run("Mainline autostart families intersect global IPv6 permission", [] {
      auto none = config({{"Plugin.mldht.autostart.IPv4", integer(0)},
          {"Plugin.mldht.autostart.IPv6", integer(0)}});
      const auto disabled = normal(&none);
      CHECK(!disabled.dht && !disabled.mainlineIpv4 && !disabled.mainlineIpv6);
      auto v6Only = config({{"Plugin.mldht.autostart.IPv4", integer(0)}});
      CHECK(!normal(&v6Only).dht);
      auto allowed = config({{"Plugin.mldht.autostart.IPv4", integer(0)}, {"IPV6 Enable Support", integer(1)}});
      const auto p = normal(&allowed);
      CHECK(p.dht && !p.mainlineIpv4 && p.mainlineIpv6);
      CHECK(p.listenPort == 6881 && p.mainlinePort == 49001);
      auto v4Only = config({{"Plugin.mldht.autostart.IPv6", integer(0)}, {"IPV6 Enable Support", integer(1)}});
      CHECK(normal(&v4Only).dht && normal(&v4Only).mainlineIpv4 && !normal(&v4Only).mainlineIpv6);
      CHECK(!engine::makeNetworkSettings(p)->get_bool(Settings::enable_dht));
    });
    run("Mainline owns an independent bounded UDP port", [] {
      auto independent = config({{"TCP.Listen.Port", integer(5001)}, {"UDP.Listen.Port", integer(5002)},
          {"peercontrol.udp.public.enable", integer(0)}, {"Server Enable UDP", integer(0)},
          {"UDP.NonData.Listen.Port.Same", integer(0)}, {"UDP.NonData.Listen.Port", integer(5003)},
          {"Plugin.mldht.port", integer(49002)}});
      const auto p = normal(&independent);
      CHECK(p.dht && p.listenPort == 5001 && p.mainlinePort == 49002);
      CHECK(engine::makeNetworkSettings(p)->get_str(Settings::listen_interfaces) == "0.0.0.0:5001");
      for (const auto port : {1, 65535}) {
        auto valid = config({{"Plugin.mldht.port", integer(port)}});
        CHECK(normal(&valid).mainlinePort == port);
      }
      for (const auto port : {-1, 0, 65536}) {
        auto invalid = config({{"Plugin.mldht.port", integer(port)}});
        const auto original = invalid.originalBytes();
        rejected(invalid, Code::InvalidConfiguration);
        CHECK(invalid.serialize() == original);
        auto off = config({{"Plugin.mldht.port", integer(port)}, {"Plugin.mldht.enable", integer(0)}});
        CHECK(!normal(&off).dht && normal(&off).mainlinePort == port);
      }
    });
    run("Mainline captured directory selects only original plugin namespace", [] {
      auto document = config({{"Plugin.mldht.enable", integer(0)}, {"Plugin.custom-ml.enable", integer(1)},
          {"Plugin.custom-ml.port", integer(49003)}, {"Plugin.custom-ml.backupOnly", integer(1)},
          {"Plugin.custom-ml.onlyPeerBootstrap", integer(1)}, {"Plugin.DHT.dht.enabled", integer(0)}});
      const auto original = document.originalBytes();
      engine::NetworkPolicyOptions options; options.mode = engine::NetworkMode::Normal;
      options.mldhtDirectory = "custom-ml";
      const auto p = engine::readNetworkPolicy(&document, options);
      CHECK(p && p->dht && p->mainlinePort == 49003 && p->mainlineBackupOnly && p->mainlinePeerBootstrapOnly);
      CHECK(!normal(&document).dht);
      CHECK(document.serialize() == original);
      auto startupOff = config({{"Plugin.custom-ml.enable", integer(1)}, {"PluginInfo.mldht.enabled", integer(0)}});
      CHECK(!engine::readNetworkPolicy(&startupOff, options)->dht);
      options.mldhtDirectory = "caf\xc3\xa9";
      auto latinKey = config({{"Plugin.caf\xe9.enable", integer(0)}, {"Plugin.caf\xe9.port", integer(49004)}});
      const auto latinBefore = latinKey.originalBytes();
      const auto latinPolicy = engine::readNetworkPolicy(&latinKey, options);
      CHECK(latinPolicy && !latinPolicy->dht && latinPolicy->mainlinePort == 49004);
      CHECK(latinKey.serialize() == latinBefore);
      auto malformedLatin = config({{"Plugin.caf\xe9.enable", bytes("invalid")}});
      CHECK(!engine::readNetworkPolicy(&malformedLatin, options));
      const auto latin = bytes("Plugin.caf\xe9.enable");
      const auto duplicateLatin = core::Document::parse("d" + latin + integer(1) + latin + integer(0) + 'e');
      CHECK(!duplicateLatin && duplicateLatin.error().code == "duplicate_key");
      options.mldhtDirectory = "../mldht";
      CHECK(!engine::readNetworkPolicy(&document, options));
      CHECK(document.serialize() == original);
    });
    run("malformed Mainline policy cannot fall back or mutate config", [] {
      for (const auto key : {"Plugin.mldht.port", "Plugin.mldht.enable", "PluginInfo.mldht.enabled",
          "Plugin.mldht.autostart.IPv4", "Plugin.mldht.backupOnly", "Plugin.mldht.onlyPeerBootstrap"}) {
        auto malformed = config({{key, bytes("not-an-integer")}});
        const auto original = malformed.originalBytes();
        rejected(malformed, Code::InvalidConfiguration);
        CHECK(malformed.serialize() == original && malformed.originalBytes() == original);
      }
      const auto key = bytes("Plugin.mldht.enable");
      const auto duplicate = core::Document::parse("d" + key + integer(1) + key + integer(0) + 'e');
      CHECK(!duplicate && duplicate.error().code == "duplicate_key");
    });
    run("Mainline changes require Discovery restart while live rates remain safe", [] {
      auto dualStack = config({{"IPV6 Enable Support", integer(1)}});
      const auto active = normal(&dualStack);
      const auto checkRestart = [&](engine::NetworkPolicy requested) {
        requested.uploadBytesPerSecond = 12345;
        const auto plan = engine::prepareLiveSettings(active, requested);
        CHECK(plan && plan->restartFields == static_cast<std::uint32_t>(engine::LiveRestartField::Discovery));
        CHECK(plan->effectivePolicy.mainlinePort == active.mainlinePort);
        CHECK(plan->effectivePolicy.mainlineIpv4 == active.mainlineIpv4 && plan->effectivePolicy.mainlineIpv6 == active.mainlineIpv6);
        CHECK(plan->effectivePolicy.mainlineBackupOnly == active.mainlineBackupOnly);
        CHECK(plan->effectivePolicy.mainlinePeerBootstrapOnly == active.mainlinePeerBootstrapOnly);
        CHECK(plan->effectivePolicy.dht == active.dht && plan->effectivePolicy.uploadBytesPerSecond == 12345);
      };
      auto requested = active; requested.mainlinePort = 49002; checkRestart(requested);
      requested = active; requested.mainlineBackupOnly = true; checkRestart(requested);
      requested = active; requested.mainlinePeerBootstrapOnly = true; checkRestart(requested);
      requested = active; requested.mainlineIpv6 = false; checkRestart(requested);
      requested = active; requested.mainlineIpv4 = false; checkRestart(requested);
      requested = active; requested.mainlinePort = 0;
      CHECK(!engine::prepareLiveSettings(active, requested));
      CHECK(active.mainlinePort == 49001 && active.dht);
    });
    run("semantic Mainline eligibility survives torrent-session DHT isolation", [] {
      const auto policy = normal();
      lt::add_torrent_params params; params.ti = metadata();
      params.dht_nodes.emplace_back("owned-node.invalid", 49001);
      CHECK(engine::applyTorrentNetworkPolicy(policy, {}, params));
      CHECK(!(params.flags & lt::torrent_flags::disable_dht) && params.dht_nodes.size() == 1);
      CHECK(!engine::makeNetworkSettings(policy)->get_bool(Settings::enable_dht));
      params.flags |= lt::torrent_flags::disable_dht;
      CHECK(engine::applyTorrentNetworkPolicy(policy, {}, params));
      CHECK((params.flags & lt::torrent_flags::disable_dht) && params.dht_nodes.empty());
    });
    run("unknown config/raw order unchanged", [] {
      auto document = config({{"opaque", "d3:raw3:\r\nXe"}, {"Max Upload Speed KBs", integer(17)}});
      const auto original = document.originalBytes();
      const auto p = normal(&document);
      CHECK(p.uploadBytesPerSecond == 17408);
      CHECK(document.serialize() == original && document.originalBytes() == original);
      CHECK(document.find("opaque")->type == core::Node::Type::Dictionary);
    });
    run("original separate LAN limits and exact well-known classification", [] {
      auto document = config({{"LAN Speed Enabled", integer(1)},
          {"Max LAN Upload Speed KBs", integer(17)}, {"Max LAN Download Speed KBs", integer(23)},
          {"Max Upload Speed KBs", integer(1)}});
      const auto before = document.serialize(); const auto policy = normal(&document);
      CHECK(policy.lanRates && normal().lanRates);
      CHECK(policy.lanUploadBytesPerSecond == 17408 && policy.lanDownloadBytesPerSecond == 23552);
      CHECK(policy.uploadBytesPerSecond == 1024 && document.serialize() == before);
      const auto filter = engine::makeNetworkPeerClassFilter(policy); CHECK(filter);
      constexpr unsigned local = 1U << static_cast<unsigned>(lt::session_handle::local_peer_class_id);
      constexpr unsigned global = 1U << static_cast<unsigned>(lt::session_handle::global_peer_class_id);
      for (const char* address : {"10.0.0.0", "10.255.255.255", "172.16.0.0", "172.31.255.255",
          "192.168.0.0", "169.254.255.255", "127.255.255.254", "::1", "fe80::1", "febf::1",
          "fec0::1", "feff::1", "::ffff:10.1.2.3", "::ffff:127.0.0.1"}) {
        CHECK(filter->access(lt::make_address(address)) == local);
      }
      for (const char* address : {"9.255.255.255", "11.0.0.0", "172.15.255.255", "172.32.0.0",
          "192.167.255.255", "169.253.255.255", "128.0.0.0", "::", "fc00::1", "fdff::1",
          "fe7f::1", "2001:db8::1", "::ffff:203.0.113.1"}) {
        CHECK(filter->access(lt::make_address(address)) == global);
      }
      auto disabled = policy; disabled.lanRates = false;
      const auto unified = engine::makeNetworkPeerClassFilter(disabled); CHECK(unified);
      CHECK(unified->access(lt::make_address("127.0.0.1")) == global);
      CHECK(unified->access(lt::make_address("10.0.0.1")) == global);
      auto isolated = engine::readNetworkPolicy(nullptr); CHECK(isolated && !isolated->lanRates);
      const auto privateClass = engine::makeNetworkPeerClassFilter(*isolated); CHECK(privateClass);
      CHECK(privateClass->access(lt::make_address("127.0.0.1")) == global);
      auto proxied = policy; proxied.bindAddresses = {"0.0.0.0"};
      proxied.proxy = {engine::NetworkProxyKind::Socks5, "127.0.0.1", {}, {}, 1080, true, false, true};
      const auto proxyFilter = engine::makeNetworkPeerClassFilter(proxied); CHECK(proxyFilter);
      CHECK(proxyFilter->access(lt::make_address("127.0.0.1")) == global);
      CHECK(proxyFilter->access(lt::make_address("127.0.0.2")) == local);
      proxied.ipv6 = true; proxied.bindAddresses = {"0.0.0.0", "::"}; proxied.proxy.host = "::1";
      const auto compressed = engine::makeNetworkPeerClassFilter(proxied); CHECK(compressed);
      CHECK(compressed->access(lt::make_address("::1")) == local);
      proxied.proxy.host = "0:0:0:0:0:0:0:1";
      const auto expanded = engine::makeNetworkPeerClassFilter(proxied); CHECK(expanded);
      CHECK(expanded->access(lt::make_address("::1")) == global);
      auto invalid = policy; invalid.lanUploadBytesPerSecond = -1;
      CHECK(!engine::makeNetworkPeerClassFilter(invalid));
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
      auto disabled = config({{"Plugin.UPnP.upnp.enable", integer(0)}, {"Plugin.mldht.enable", integer(0)}});
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
    run("Mainline semantic permission cannot invalidate independent proxy routes", [] {
      auto http = config({{"Enable.Proxy", integer(1)}, {"Proxy.Host", bytes("127.0.0.1")},
          {"Proxy.Port", bytes("8080")}, {"Server Enable UDP", integer(0)}});
      const auto httpBefore = http.originalBytes();
      const auto httpPolicy = normal(&http);
      CHECK(httpPolicy.dht && httpPolicy.proxy.kind == engine::NetworkProxyKind::Http);
      const auto httpSettings = engine::makeNetworkSettings(httpPolicy);
      CHECK(httpSettings && httpSettings->get_int(Settings::proxy_type) == Settings::http);
      CHECK(httpSettings->get_bool(Settings::proxy_tracker_connections) && !httpSettings->get_bool(Settings::proxy_peer_connections));
      CHECK(httpSettings->get_bool(Settings::enable_outgoing_utp)); // Explicit direct data route.
      CHECK(!httpSettings->get_bool(Settings::enable_dht) && httpSettings->get_str(Settings::dht_bootstrap_nodes).empty());
      CHECK(http.serialize() == httpBefore);
      CHECK(http.replaceValue("Server Enable UDP", integer(1)));
      rejected(http, Code::Unsupported); // HTTP still cannot proxy UDP trackers.
      auto socks4 = config({{"Proxy.Data.Enable", integer(1)}, {"Proxy.Data.SOCKS.version", bytes("V4")},
          {"Proxy.Host", bytes("127.0.0.1")}, {"Proxy.Port", bytes("1080")},
          {"peercontrol.udp.public.enable", integer(0)}, {"Server Enable UDP", integer(0)}});
      const auto socksBefore = socks4.originalBytes();
      const auto socksPolicy = normal(&socks4);
      CHECK(socksPolicy.dht && socksPolicy.proxy.kind == engine::NetworkProxyKind::Socks4);
      const auto socksSettings = engine::makeNetworkSettings(socksPolicy);
      CHECK(socksSettings && socksSettings->get_int(Settings::proxy_type) == Settings::socks4);
      CHECK(!socksSettings->get_bool(Settings::enable_dht) && socksSettings->get_str(Settings::dht_bootstrap_nodes).empty());
      CHECK(!socksSettings->get_bool(Settings::enable_outgoing_utp) && socks4.serialize() == socksBefore);
      CHECK(socks4.replaceValue("peercontrol.udp.public.enable", integer(1)));
      rejected(socks4, Code::Unsupported); // SOCKS4 still cannot proxy uTP peers.
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
