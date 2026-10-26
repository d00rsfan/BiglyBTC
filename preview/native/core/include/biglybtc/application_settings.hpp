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
enum class SettingsSection { Connection, Advanced, Proxy, Transfer, Queue, Files, Seeding, FirstPriority, Ignore };
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
  DefaultSavePath, RenameIncomplete, IncompleteSuffix,
  LinkMinimumDownloads, IgnoreChecking, MaximumActiveWhenSeedingEnabled, MaximumActiveWhenSeeding, RankMode, DownloadOrder, PreferLargerSwarms, SeedCountFallback, MinimumPeersToBoostNoSeeds, MinimumDownloadBytesPerSecond, MinimumSeedBytesPerSecond, MinimumSeedSeconds, MinimumSeedActiveSeconds, TimedMinimumWithPeersSeconds, TimedMaximumWithPeersSeconds, MaximumStalledSeeds, StalledSeedsIgnoreZeroPeers, AutoStartZeroPeers, FirstPriorityMode, FirstPriorityShareRatio, FirstPrioritySeedMinutes, FirstPriorityActiveMinutes, FirstPriorityIgnoreSeedPeerRatio, FirstPriorityIgnoreIdleMinutes, FirstPriorityIgnoreZeroPeers, IgnoreShareRatio, IgnoreShareRatioSeedStart, IgnoreSeedCount, IgnorePeerSeedRatio, IgnorePeerSeedRatioSeedStart, IgnoreZeroPeers, PeersAsFullCopy, FakeFullCopySeedStart, StopOnceBandwidthMet, PreventNewSeedsAtUploadLimit, UploadLimitSlack, UploadLimitSlackIsPercent, NewSeedsMoveTop, RetainForceStartOnComplete, LightSeedingEnabled, ReservedLightSeedSlots, FlexibleSeedSlots, OriginalQueueOption0, OriginalQueueOption1, OriginalQueueOption2, OriginalQueueOption3, OriginalQueueOption4, OriginalQueueOption5, OriginalQueueOption6, OriginalQueueOption7, CheckBindOnStart, Count
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
  string(BindIp,Advanced,"Bind IP","Bind addresses",std::string_view{},1024,A::Restart,SettingStringRule::Text,false,"Numeric addresses or interface selectors separated by semicolons, for example 127.0.0.1 or eth0[v4]. Explicit bindings are checked before startup and while the owner runs."),
  boolean(Ipv6,Advanced,"IPV6 Enable Support","IPv6",false,A::Restart),
  boolean(EnforceBind,Advanced,"Enforce Bind IP","Enforce binding",false,A::Restart,"Requires an assigned address or interface. Missing or lost bindings stop the native owner; there is no automatic unbound fallback."),
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
  string(PeerProxyVersion,Proxy,"Proxy.Data.SOCKS.version","Peer SOCKS version",std::string_view{"V4"},3,A::Restart,SettingStringRule::ProxyVersion),
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
  integer(MaxActive,Queue,"max active torrents","Maximum active torrents",4,0,1000000,A::Live,"Zero is unlimited; original queue rules control ordinary torrents. Forced downloads bypass the cap."),
  integer(MaxDownloads,Queue,"max downloads","Maximum downloading torrents",4,0,1000000,A::Live,"Zero is unlimited; forced handles bypass automatic queueing."),
  integer(MinDownloads,Queue,"min downloads","Minimum downloads",1,0,1000000,A::Live,"Reserves download slots within the original queue policy."),
  string(DefaultSavePath,Files,"Default save path","Default download directory",std::nullopt,4096,A::NextAdmission,SettingStringRule::AbsolutePath,false,"Original default is platform-derived; no path is inferred here. Does not move existing payloads."),
  boolean(RenameIncomplete,Files,"Rename Incomplete Files","Add suffix while incomplete",false,A::OriginalOnly,"Saved for the original client; native new-import suffix naming is not implemented. Existing payloads are not renamed."),
  string(IncompleteSuffix,Files,"Rename Incomplete Files Extension","Incomplete suffix",std::string_view{".az!"},64,A::OriginalOnly,SettingStringRule::Suffix,false,"Saved for the original client; native new-import suffix naming is not implemented."),
  boolean(LinkMinimumDownloads,Queue,"StartStopManager_bMaxMinDLLinked","Link minimum downloads to maximum",false,A::Live,""),
  boolean(IgnoreChecking,Queue,"StartStopManager_bMaxDownloadIgnoreChecking","Do not count checking downloads",false,A::Live,""),
  boolean(MaximumActiveWhenSeedingEnabled,Queue,"StartStopManager_bMaxActiveTorrentsWhenSeedingEnabled","Separate active limit when only seeding",false,A::Live,""),
  integer(MaximumActiveWhenSeeding,Queue,"StartStopManager_iMaxActiveTorrentsWhenSeeding","Active limit when only seeding",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(RankMode,Seeding,"StartStopManager_iRankType","Seed ranking mode",1,0,4,A::Live,"0 queue order; 1 seed/peer ratio; 2 seed count; 3 timed rotation; 4 peer count."),
  integer(DownloadOrder,Queue,"StartStopManager_Downloading_iSortType","Download ordering",0,0,8,A::Live,"0 position, 1 seeds, 2 speed, 3 reverse seeds, 4 size, 5 reverse size, 6 ETA, 7 file priority, 8 remaining. File-priority order uses verified original-file observations. Speed probes use the qualified counter scope available to this owner; payload-only providers are marked partial. Smoothed ETA and capped global smoothing require additional observed providers."),
  boolean(PreferLargerSwarms,Seeding,"StartStopManager_bPreferLargerSwarms","Prefer larger swarms",true,A::Live,""),
  integer(SeedCountFallback,Seeding,"StartStopManager_iRankTypeSeedFallback","Seed-count fallback threshold",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MinimumPeersToBoostNoSeeds,Seeding,"StartStopManager_iMinPeersToBoostNoSeeds","Minimum peers to boost torrents without seeds",1,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MinimumDownloadBytesPerSecond,Queue,"StartStopManager_iMinSpeedForActiveDL","Active download threshold (B/s)",512,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MinimumSeedBytesPerSecond,Seeding,"StartStopManager_iMinSpeedForActiveSeeding","Active seeding threshold (B/s)",512,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MinimumSeedSeconds,Seeding,"StartStopManager_iMinSeedingTime","Minimum seeding time (seconds)",300,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MinimumSeedActiveSeconds,Seeding,"StartStopManager_iMinSeedingTimeWhenActive","Minimum active seeding time (seconds)",900,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(TimedMinimumWithPeersSeconds,Seeding,"StartStopManager_iTimed_MinSeedingTimeWithPeers","Timed rotation: minimum with peers (seconds)",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(TimedMaximumWithPeersSeconds,Seeding,"StartStopManager_iTimed_MaxSeedingTimeWithPeers","Timed rotation: maximum with peers (seconds)",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(MaximumStalledSeeds,Seeding,"StartStopManager_iMaxStalledSeeding","Maximum stalled seeds",5,0,std::numeric_limits<int>::max(),A::Live,""),
  boolean(StalledSeedsIgnoreZeroPeers,Seeding,"StartStopManager_bMaxStalledSeedingIgnoreZP","Exclude torrents without peers from stalled limit",true,A::Live,""),
  boolean(AutoStartZeroPeers,Seeding,"StartStopManager_bAutoStart0Peers","Automatically start torrents without peers",false,A::Live,""),
  integer(FirstPriorityMode,FirstPriority,"StartStopManager_iFirstPriority_Type","Match first-priority criteria",1,0,1,A::Live,"0 requires all enabled criteria; 1 accepts any enabled criterion."),
  integer(FirstPriorityShareRatio,FirstPriority,"StartStopManager_iFirstPriority_ShareRatio","First-priority share ratio (thousandths)",500,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(FirstPrioritySeedMinutes,FirstPriority,"StartStopManager_iFirstPriority_SeedingMinutes","First-priority seeding time (minutes)",0,0,std::numeric_limits<int>::max()/60,A::Live,""),
  integer(FirstPriorityActiveMinutes,FirstPriority,"StartStopManager_iFirstPriority_DLMinutes","First-priority active time (minutes)",0,0,std::numeric_limits<int>::max()/60,A::Live,""),
  integer(FirstPriorityIgnoreSeedPeerRatio,FirstPriority,"StartStopManager_iFirstPriority_ignoreSPRatio","Exclude above seed/peer ratio",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(FirstPriorityIgnoreIdleMinutes,FirstPriority,"StartStopManager_iFirstPriority_ignoreIdleMinutes","Exclude after upload idle time (minutes)",0,0,std::numeric_limits<int>::max(),A::Live,""),
  boolean(FirstPriorityIgnoreZeroPeers,FirstPriority,"StartStopManager_bFirstPriority_ignore0Peer","Exclude known torrents without peers",true,A::Live,"Original default is false for ui=az2, true otherwise. Unknown tracker counts do not mean zero peers."),
  string(IgnoreShareRatio,Ignore,"Stop Ratio","Stop share ratio",std::string_view{"0"},64,A::Live,SettingStringRule::Text,false,"Original Float stored as decimal bytes; native policy converts binary32 to thousandths. No full-default rewrite."),
  integer(IgnoreShareRatioSeedStart,Ignore,"StartStopManager_iIgnoreShareRatioSeedStart","Apply share ratio from this seed count",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(IgnoreSeedCount,Ignore,"StartStopManager_iIgnoreSeedCount","Stop above this seed count",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(IgnorePeerSeedRatio,Ignore,"Stop Peers Ratio","Stop peer/seed ratio",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(IgnorePeerSeedRatioSeedStart,Ignore,"StartStopManager_iIgnoreRatioPeersSeedStart","Apply peer/seed ratio from this seed count",0,0,std::numeric_limits<int>::max(),A::Live,""),
  boolean(IgnoreZeroPeers,Ignore,"StartStopManager_bIgnore0Peers","Ignore torrents with no peers",true,A::Live,""),
  integer(PeersAsFullCopy,Seeding,"StartStopManager_iNumPeersAsFullCopy","Peers per equivalent full copy",0,0,std::numeric_limits<int>::max(),A::Live,""),
  integer(FakeFullCopySeedStart,Seeding,"StartStopManager_iFakeFullCopySeedStart","Count equivalent copies from this seed count",0,0,std::numeric_limits<int>::max(),A::Live,""),
  boolean(StopOnceBandwidthMet,Queue,"StartStopManager_bStopOnceBandwidthMet","Stop queue additions when bandwidth is full",true,A::Live,""),
  boolean(PreventNewSeedsAtUploadLimit,Seeding,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMet","Do not start seeds at upload limit",false,A::Live,""),
  integer(UploadLimitSlack,Seeding,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMetSlack","Upload-limit threshold",95,0,std::numeric_limits<int>::max(),A::Live,""),
  boolean(UploadLimitSlackIsPercent,Seeding,"StartStopManager_bStartNoMoreSeedsWhenUpLimitMetPercent","Upload threshold is a percentage",true,A::Live,""),
  boolean(NewSeedsMoveTop,Queue,"StartStopManager_bNewSeedsMoveTop","Move newly completed seeds to top",true,A::Live,"Applied when every saved completion group has a current checked owner; otherwise positions remain unchanged and unavailable."),
  boolean(RetainForceStartOnComplete,Queue,"StartStopManager_bRetainForceStartWhenComplete","Keep force-start after completion",false,A::Live,"Transition intent only; worker persists original force/pool ordering before engine effects."),
  boolean(LightSeedingEnabled,Seeding,"Enable Light Seeding","Light seeding",true,A::OriginalOnly,"Queued lightweight tracker/activation service is unavailable; this saved setting is preserved for the original client."),
  integer(ReservedLightSeedSlots,Seeding,"Light Seed Slots Reserved","Reserved light-seed slots",4,0,std::numeric_limits<int>::max(),A::OriginalOnly,"Queued lightweight tracker/activation service is unavailable; this saved setting is preserved for the original client."),
  integer(FlexibleSeedSlots,Seeding,"Flexible Seed Slots","Flexible seed slots",5,0,std::numeric_limits<int>::max(),A::Live,"Limits temporary global overlap while seed ranks change. It does not increase the ordinary active-torrent quota."),
  integer(OriginalQueueOption0,Seeding,"StartStopManager_Downloading_iTestTimeSecs","Speed probe duration (seconds)",120,0,std::numeric_limits<int>::max()/1000,A::Live,"Ordinary probes use the source-qualified payload+protocol prefix when available, otherwise explicit payload-only scope. Smoothed ETA and capped global smoothing still need observed providers."),
  integer(OriginalQueueOption1,Seeding,"StartStopManager_Downloading_iRetestTimeMins","Speed probe retest (minutes)",30,0,std::numeric_limits<int>::max()/60000,A::Live,"Ordinary probes use the source-qualified payload+protocol prefix when available, otherwise explicit payload-only scope. Smoothed ETA and capped global smoothing still need observed providers."),
  boolean(OriginalQueueOption2,Seeding,"StartStopManager_Downloading_bTestActive","Probe active downloads",false,A::Live,"Allow ordinary active downloads to be tested after queued candidates. Manual pause and force-start authority are preserved."),
  boolean(OriginalQueueOption3,Seeding,"StartStopManager_bAutoReposition","StartStopManager_bAutoReposition",false,A::OriginalOnly,"saved pool-position normalization outside pure reducer"),
  boolean(OriginalQueueOption4,Seeding,"Disconnect Seed","Disconnect Seed",true,A::OriginalOnly,"peer connection policy outside queue reducer"),
  boolean(OriginalQueueOption5,Seeding,"Use Super Seeding","Use Super Seeding",false,A::OriginalOnly,"torrent transport feature outside queue reducer"),
  integer(OriginalQueueOption6,Seeding,"StartStopManager_iAddForSeedingDLCopyCount","StartStopManager_iAddForSeedingDLCopyCount",1,0,std::numeric_limits<int>::max(),A::OriginalOnly,"new seed admission accounting outside reducer"),
  boolean(OriginalQueueOption7,Seeding,"StartStopManager_bAddForDownloadingSR1","StartStopManager_bAddForDownloadingSR1",true,A::OriginalOnly,"new download admission accounting outside reducer"),
  boolean(CheckBindOnStart,Advanced,"Check Bind IP On Start","Check bindings on startup",true,A::Restart,"Checks configured bindings before startup. Native explicit bindings always fail closed even when this warning preference is disabled.")
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
    case SettingStringRule::ProxyVersion: return *string == "V4" || *string == "V4a" || *string == "V5";
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
