#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <type_traits>

namespace biglybtc::core {
enum class SettingsSection { Connection, Advanced, Proxy, Transfer, Queue, Files };
enum class SettingsApplicability { Live, NextAdmission, Restart, OriginalOnly };
enum class SettingKind { Boolean, Integer, String };
enum class SettingStringRule { Text, Port, ProxyVersion, AbsolutePath, Suffix };
enum class SettingId {
  TcpPort, TcpEnabled, UdpPort, UdpEnabled, PublicTcp, PublicUtp,
  Trackers, HttpTrackers, UdpTrackers, Dht, DhtPlugin, PeerExchange,
  BindIp, Ipv6, EnforceBind, RandomizePorts, TcpOverride, HttpDataEnabled,
  AdditionalBinds, IgnoreBindForLan, UdpNonDataSame, UdpNonDataPort, RequireEncryption,
  ProxyEnabled, ProxySocks, ProxyHost, ProxyPort, ProxyUsername, ProxyPassword,
  PeerProxyEnabled, PeerProxySame, PeerProxyVersion, PeerProxyHost, PeerProxyPort,
  PeerProxyUsername, PeerProxyPassword, ProxyRemoteDns,
  UploadLimit, DownloadLimit, SeedingUploadLimit, SeedingUploadEnabled,
  AutoUpload, AutoSeedingUpload, LanRates,
  MaxActive, MaxDownloads, MinDownloads,
  DefaultSavePath, RenameIncomplete, IncompleteSuffix, Count
};
using SettingValue = std::variant<bool, std::int64_t, std::string>;
using SettingDefault = std::variant<bool, std::int64_t, std::string_view>;
struct SettingField {
  SettingId id;
  SettingsSection section;
  std::string_view key, label, help;
  SettingKind kind;
  SettingsApplicability applicability;
  std::optional<SettingDefault> defaultValue;
  std::int64_t minimum = 0, maximum = 0;
  std::size_t maximumBytes = 0;
  SettingStringRule stringRule = SettingStringRule::Text;
  bool secret = false;
};
namespace settings_detail {
constexpr SettingField boolean(SettingId id, SettingsSection section, std::string_view key,
    std::string_view label, bool value, SettingsApplicability applies, std::string_view help = {}) {
  return {id, section, key, label, help, SettingKind::Boolean, applies, SettingDefault{value}};
}
constexpr SettingField integer(SettingId id, SettingsSection section, std::string_view key,
    std::string_view label, std::int64_t value, std::int64_t low, std::int64_t high,
    SettingsApplicability applies, std::string_view help = {}) {
  return {id, section, key, label, help, SettingKind::Integer, applies, SettingDefault{value}, low, high};
}
constexpr SettingField string(SettingId id, SettingsSection section, std::string_view key,
    std::string_view label, std::optional<std::string_view> value, std::size_t max,
    SettingsApplicability applies, SettingStringRule rule = SettingStringRule::Text,
    bool secret = false, std::string_view help = {}) {
  return {id, section, key, label, help, SettingKind::String, applies,
      value ? std::optional{SettingDefault{*value}} : std::nullopt, 0, 0, max, rule, secret};
}
using enum SettingId;
using enum SettingsSection;
using A = SettingsApplicability;
inline constexpr std::array Catalog{
  integer(TcpPort,Connection,"TCP.Listen.Port","TCP listen port",6881,1,65535,A::Restart),
  boolean(TcpEnabled,Connection,"TCP.Listen.Port.Enable","Accept TCP connections",true,A::Restart),
  integer(UdpPort,Connection,"UDP.Listen.Port","UDP listen port",6881,1,65535,A::Restart),
  boolean(UdpEnabled,Connection,"UDP.Listen.Port.Enable","Accept uTP connections",true,A::Restart),
  boolean(PublicTcp,Connection,"peercontrol.tcp.public.enable","TCP peer transport",true,A::Restart),
  boolean(PublicUtp,Connection,"peercontrol.udp.public.enable","uTP peer transport",true,A::Restart),
  boolean(Trackers,Connection,"Peer Source Selection Default.Tracker","Tracker discovery",true,A::Restart),
  boolean(HttpTrackers,Connection,"Tracker Client Enable TCP","HTTP/HTTPS trackers",true,A::Restart),
  boolean(UdpTrackers,Connection,"Server Enable UDP","UDP trackers",true,A::Restart),
  boolean(Dht,Connection,"Peer Source Selection Default.DHT","DHT peer discovery",true,A::Restart),
  boolean(DhtPlugin,Connection,"Plugin.DHT.dht.enabled","Original DHT enable flag",true,A::Restart,"Controls the native normal-session discovery policy; Azureus owned service remains separate."),
  boolean(PeerExchange,Connection,"Peer Source Selection Default.PeerExchange","Peer exchange",true,A::Restart),
  string(BindIp,Advanced,"Bind IP","Bind addresses",std::string_view{},1024,A::Restart,SettingStringRule::Text,false,"Numeric addresses separated by semicolons; interface-name binding is unsupported."),
  boolean(Ipv6,Advanced,"IPV6 Enable Support","IPv6",false,A::Restart),
  boolean(EnforceBind,Advanced,"Enforce Bind IP","Enforce binding",false,A::Restart,"Requires a supported explicit numeric bind address."),
  boolean(RandomizePorts,Advanced,"Listen.Port.Randomize.Enable","Randomize ports",false,A::OriginalOnly,"Native routing currently requires this to be disabled."),
  string(TcpOverride,Advanced,"TCP.Listen.Port.Override","Advertised TCP port override",std::string_view{},64,A::OriginalOnly,SettingStringRule::Text,false,"Native routing currently requires this to be empty."),
  boolean(HttpDataEnabled,Advanced,"HTTP.Data.Listen.Port.Enable","HTTP data listener",false,A::OriginalOnly,"Native routing currently requires this to be disabled."),
  string(AdditionalBinds,Advanced,"Additional Service Bind IPs","Additional service binds",std::string_view{},1024,A::OriginalOnly,SettingStringRule::Text,false,"Native routing currently requires this to be empty."),
  boolean(IgnoreBindForLan,Advanced,"Ignore Bind For LAN","Ignore binding for LAN",false,A::OriginalOnly,"Native routing currently requires this to be disabled."),
  boolean(UdpNonDataSame,Advanced,"UDP.NonData.Listen.Port.Same","Use same UDP discovery port",true,A::Restart),
  integer(UdpNonDataPort,Advanced,"UDP.NonData.Listen.Port","UDP discovery port",6881,1,65535,A::Restart),
  boolean(RequireEncryption,Advanced,"network.transport.encrypted.require","Require encrypted peers",false,A::Restart),
  boolean(ProxyEnabled,Proxy,"Enable.Proxy","Proxy tracker connections",false,A::Restart),
  boolean(ProxySocks,Proxy,"Enable.SOCKS","Use SOCKS for trackers",false,A::Restart),
  string(ProxyHost,Proxy,"Proxy.Host","Proxy host",std::string_view{},253,A::Restart),
  string(ProxyPort,Proxy,"Proxy.Port","Proxy port",std::string_view{},5,A::Restart,SettingStringRule::Port),
  string(ProxyUsername,Proxy,"Proxy.Username","Proxy username",std::string_view{"<none>"},1024,A::Restart),
  string(ProxyPassword,Proxy,"Proxy.Password","Proxy password",std::string_view{},1024,A::Restart,SettingStringRule::Text,true),
  boolean(PeerProxyEnabled,Proxy,"Proxy.Data.Enable","Proxy peer connections",false,A::Restart),
  boolean(PeerProxySame,Proxy,"Proxy.Data.Same","Use tracker proxy for peers",true,A::Restart),
  string(PeerProxyVersion,Proxy,"Proxy.Data.SOCKS.version","Peer SOCKS version",std::string_view{"V4"},2,A::Restart,SettingStringRule::ProxyVersion),
  string(PeerProxyHost,Proxy,"Proxy.Data.Host","Peer proxy host",std::string_view{},253,A::Restart),
  string(PeerProxyPort,Proxy,"Proxy.Data.Port","Peer proxy port",std::string_view{},5,A::Restart,SettingStringRule::Port),
  string(PeerProxyUsername,Proxy,"Proxy.Data.Username","Peer proxy username",std::string_view{"<none>"},1024,A::Restart),
  string(PeerProxyPassword,Proxy,"Proxy.Data.Password","Peer proxy password",std::string_view{},1024,A::Restart,SettingStringRule::Text,true),
  boolean(ProxyRemoteDns,Proxy,"Proxy.SOCKS.Tracker.DNS.Disable","Resolve tracker names through proxy",true,A::Restart),
  integer(UploadLimit,Transfer,"Max Upload Speed KBs","Upload limit (KiB/s)",0,0,std::numeric_limits<int>::max()/1024,A::Live,"Zero is unlimited. Native bounded editor range; pre-existing negative original unlimited values are retained."),
  integer(DownloadLimit,Transfer,"Max Download Speed KBs","Download limit (KiB/s)",0,0,std::numeric_limits<int>::max()/1024,A::Live,"Zero is unlimited."),
  integer(SeedingUploadLimit,Transfer,"Max Upload Speed Seeding KBs","Seeding upload limit (KiB/s)",0,0,std::numeric_limits<int>::max()/1024,A::Live,"Zero is unlimited."),
  boolean(SeedingUploadEnabled,Transfer,"enable.seedingonly.upload.rate","Use separate seeding upload limit",false,A::Live),
  boolean(AutoUpload,Transfer,"Auto Upload Speed Enabled","Automatic upload control",false,A::OriginalOnly,"Native passive estimates do not control rates; native routing requires disabled."),
  boolean(AutoSeedingUpload,Transfer,"Auto Upload Speed Seeding Enabled","Automatic seeding upload control",false,A::OriginalOnly,"Native routing requires disabled."),
  boolean(LanRates,Transfer,"LAN Speed Enabled","Separate LAN rates",true,A::OriginalOnly,"Original default is enabled. Native routing has no separate LAN rates: absent uses its disabled fallback; explicitly enabled must be disabled to repair routing."),
  integer(MaxActive,Queue,"max active torrents","Maximum active torrents",4,0,1000000,A::Live,"Zero is unlimited; native engine cap, not all original seeding rules."),
  integer(MaxDownloads,Queue,"max downloads","Maximum downloading torrents",4,0,1000000,A::Live,"Zero is unlimited; forced handles bypass automatic queueing."),
  integer(MinDownloads,Queue,"min downloads","Minimum downloads",1,0,1000000,A::OriginalOnly,"Original scheduler policy; retained for the original client."),
  string(DefaultSavePath,Files,"Default save path","Default download directory",std::nullopt,4096,A::NextAdmission,SettingStringRule::AbsolutePath,false,"Original default is platform-derived; no path is inferred here. Does not move existing payloads."),
  boolean(RenameIncomplete,Files,"Rename Incomplete Files","Add suffix while incomplete",false,A::OriginalOnly,"Saved for the original client; native new-import suffix naming is not implemented. Existing payloads are not renamed."),
  string(IncompleteSuffix,Files,"Rename Incomplete Files Extension","Incomplete suffix",std::string_view{".az!"},64,A::OriginalOnly,SettingStringRule::Suffix,false,"Saved for the original client; native new-import suffix naming is not implemented.")
};
static_assert(Catalog.size() == static_cast<std::size_t>(SettingId::Count));
} // namespace settings_detail
inline std::span<const SettingField> applicationSettingsCatalog() noexcept { return settings_detail::Catalog; }
inline const SettingField* applicationSetting(SettingId id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < settings_detail::Catalog.size() ? &settings_detail::Catalog[index] : nullptr;
}
inline std::optional<SettingValue> applicationSettingDefault(const SettingField& field) {
  if (!field.defaultValue) { return {}; }
  return std::visit([](const auto& value) -> SettingValue {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, std::string_view>) { return std::string(value); }
    else { return value; }
  }, *field.defaultValue);
}
inline bool validApplicationSettingValue(const SettingField& field, const SettingValue& value) noexcept {
  if (field.kind == SettingKind::Boolean) { return std::holds_alternative<bool>(value); }
  if (field.kind == SettingKind::Integer) {
    const auto* number = std::get_if<std::int64_t>(&value);
    return number && *number >= field.minimum && *number <= field.maximum;
  }
  const auto* string = std::get_if<std::string>(&value);
  if (!string || string->size() > field.maximumBytes) { return false; }
  for (const unsigned char c : *string) { if (c < 32 || c == 127) { return false; } }
  switch (field.stringRule) {
    case SettingStringRule::Text: return true;
    case SettingStringRule::Port: {
      if (string->empty()) { return true; } // Required only when that proxy is enabled.
      unsigned int port = 0;
      const auto parsed = std::from_chars(string->data(), string->data()+string->size(),port);
      return parsed.ec == std::errc{} && parsed.ptr == string->data()+string->size() && port >= 1 && port <= 65535;
    }
    case SettingStringRule::ProxyVersion: return *string == "V4" || *string == "V5";
    case SettingStringRule::AbsolutePath: {
      if (string->empty() || string->front() != '/' || string->back() == '/') { return *string == "/"; }
      std::size_t begin = 1;
      for (std::size_t i=1;i<=string->size();++i) {
        if (i==string->size() || (*string)[i]=='/') {
          const auto part=std::string_view(*string).substr(begin,i-begin);
          if (part.empty() || part=="." || part=="..") { return false; } begin=i+1;
        }
      }
      return true;
    }
    case SettingStringRule::Suffix: return !string->empty() && string->find('/') == std::string::npos && string->find('\\') == std::string::npos;
  }
  return false;
}
struct SettingChange { SettingId id; SettingValue value; };
} // namespace biglybtc::core
