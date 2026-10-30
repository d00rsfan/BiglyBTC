// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "managed_payload_counters.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/biglybtc_payload_counters.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>

namespace biglybtc::services::i2p::provider::detail {
struct RetirementCounters {
  engine::detail::ManagedPayloadCounters payload;
  std::atomic<bool> checked{false};
};

// This factory is ONLY installed after privateOwnerRuntimePin has verified all
// 31 exports in the actual mapped private image. It does not grant the separate
// PersistentSession runtime guard, or load/approve another provider.
class RetirementCounterPlugin final:public libtorrent::torrent_plugin {
public:
  RetirementCounterPlugin(libtorrent::torrent_handle handle,
      std::shared_ptr<RetirementCounters> counters,std::shared_ptr<void> pin)
      :handle_(std::move(handle)),counters_(std::move(counters)),pin_(std::move(pin)) {
    counters_->payload.installed(true,true);
  }
  bool on_pause()noexcept override {paused_=true;return false;}
  bool on_resume()noexcept override {
    if(!paused_){return false;}paused_=false;
    const auto raw=read();if(!raw){counters_->payload.fail();return false;}
    const auto& payload=raw->traffic.payload;
    if(!payload.files_checked&&(payload.received||payload.sent)){counters_->payload.fail();return false;}
    counters_->payload.seal(payload.received,payload.sent,protocol(*raw),bad(*raw));
    return false; // Ordinary LT resume subsequently clears the raw epoch.
  }
  void on_files_checked()noexcept override {
    const auto raw=read();
    if(!raw||raw->traffic.payload.received||raw->traffic.payload.sent){counters_->payload.fail();return;}
    // The SDK also invokes this at extension attachment for known metadata.
    // Only the owner's matched torrent_checked_alert may certify completion.
    if(!protocol(*raw)){counters_->payload.combinedUnavailable();}
    if(!bad(*raw)){counters_->payload.badUnavailable();}
  }
private:
  static std::optional<engine::detail::ManagedPayloadCounters::Protocol> protocol(
      const libtorrent::BiglybtcTrafficCountersV3& raw)noexcept {
    if(raw.traffic.reserved||raw.traffic.protocol_known!=1||raw.traffic.protocol_received<0||raw.traffic.protocol_sent<0){return {};}
    return engine::detail::ManagedPayloadCounters::Protocol{raw.traffic.protocol_received,raw.traffic.protocol_sent};
  }
  static std::optional<engine::detail::ManagedPayloadCounters::Bad> bad(
      const libtorrent::BiglybtcTrafficCountersV3& raw)noexcept {
    if(raw.reserved||raw.bad_known!=1||raw.failed_bytes<0||raw.redundant_bytes<0
        ||raw.failed_bytes==std::numeric_limits<std::int64_t>::max()
        ||raw.redundant_bytes==std::numeric_limits<std::int64_t>::max()){return {};}
    return engine::detail::ManagedPayloadCounters::Bad{raw.redundant_bytes,raw.failed_bytes};
  }
  std::optional<libtorrent::BiglybtcTrafficCountersV3> read()const noexcept {
    if(!pin_){return {};}libtorrent::BiglybtcTrafficCountersV3 raw{};
    if(libtorrent::biglybtc_read_traffic_counters_v3(&handle_,&raw,sizeof(raw))
        !=static_cast<std::uint32_t>(libtorrent::BiglybtcTrafficCounterStatusV3::Accepted)
        ||raw.traffic.payload.reserved||raw.traffic.payload.files_checked>1
        ||raw.traffic.payload.received<0||raw.traffic.payload.sent<0){return {};}
    return raw;
  }
  libtorrent::torrent_handle handle_;
  std::shared_ptr<RetirementCounters> counters_;
  std::shared_ptr<void> pin_;
  bool paused_=true;
};
}
