#include "managed_payload_counters.hpp"
#include "managed_queue_observations.hpp"
#include "persistent_runtime_guard.hpp"
#include "original_torrent_identity.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <utility>

#if defined(__linux__) && __has_include(<libtorrent/biglybtc_payload_counters.hpp>) \
    && __has_include(<libtorrent/biglybtc_swarm.hpp>) && !defined(TORRENT_DISABLE_EXTENSIONS)
#define BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS 1
#include <libtorrent/biglybtc_payload_counters.hpp>
#include <libtorrent/biglybtc_swarm.hpp>
#include <dlfcn.h>
#else
#define BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS 0
#endif
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS && defined(TORRENT_BIGLYBTC_TRAFFIC_COUNTERS_ABI) \
    && TORRENT_BIGLYBTC_TRAFFIC_COUNTERS_ABI == 2
#define BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS 1
#else
#define BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS 0
#endif

#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS && __has_include(<libtorrent/biglybtc_queue_observer.hpp>)
#include <libtorrent/biglybtc_queue_observer.hpp>
#define BIGLYBTC_SUPPORTED_QUEUE_OBSERVER 1
#else
#define BIGLYBTC_SUPPORTED_QUEUE_OBSERVER 0
#endif

namespace biglybtc::engine::detail {
static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "Managed raw reset boundaries require the reviewed LT2.0.12 source");

struct ManagedPayloadCounterBridge final {
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  std::shared_ptr<void> library;
  decltype(&libtorrent::biglybtc_read_payload_counters_v1) read = nullptr;
  decltype(&libtorrent::biglybtc_swarm_mask_abi_v1) swarm = nullptr;
#if BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
  decltype(&libtorrent::biglybtc_read_traffic_counters_v2) readTraffic = nullptr;
#endif
#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER
  decltype(&libtorrent::biglybtc_attach_queue_observer_v1) attachQueue = nullptr;
#endif
#endif
};

std::shared_ptr<const ManagedPayloadCounterBridge> managedPayloadCounterBridge() noexcept {
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  try {
    std::shared_ptr<void> library;
#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER
    decltype(&libtorrent::biglybtc_attach_queue_observer_v1) attachQueue=nullptr;
    // Full THIRTEEN-export same-image source/artifact proof; never marker-only.
    auto queuePin=managedQueueObserverRuntimePin();
    if(queuePin){
      const auto abi=reinterpret_cast<decltype(&libtorrent::biglybtc_queue_observer_abi_v1)>(::dlsym(queuePin.get(),"biglybtc_queue_observer_abi_v1"));
      const auto attach=reinterpret_cast<decltype(attachQueue)>(::dlsym(queuePin.get(),"biglybtc_attach_queue_observer_v1"));
      if(abi&&attach&&abi()==1
          &&reinterpret_cast<void*>(abi)==::dlsym(RTLD_DEFAULT,"biglybtc_queue_observer_abi_v1")
          &&reinterpret_cast<void*>(attach)==::dlsym(RTLD_DEFAULT,"biglybtc_attach_queue_observer_v1")){
        library=std::move(queuePin);attachQueue=attach;
      }
    }
#endif
#if BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
    decltype(&libtorrent::biglybtc_read_traffic_counters_v2) traffic = nullptr;
    // Optional V2 requires the complete ELEVEN-export source/artifact/mapping
    // proof and transfers that SAME held object. A header/marker never grants it.
    auto extended = library ? library : managedPayloadCounterRuntimePin(true);
    if (extended) {
      const auto abi = reinterpret_cast<decltype(&libtorrent::biglybtc_traffic_counter_abi_v2)>(
          ::dlsym(extended.get(), "biglybtc_traffic_counter_abi_v2"));
      const auto read = reinterpret_cast<decltype(traffic)>(
          ::dlsym(extended.get(), "biglybtc_read_traffic_counters_v2"));
      if (abi && read && abi() == 2
          && reinterpret_cast<void*>(abi) == ::dlsym(RTLD_DEFAULT, "biglybtc_traffic_counter_abi_v2")
          && reinterpret_cast<void*>(read) == ::dlsym(RTLD_DEFAULT, "biglybtc_read_traffic_counters_v2")) {
        library = std::move(extended); traffic = read;
      }
    }
#endif
#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER && BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
    if(!traffic){attachQueue=nullptr;}
#endif
    // Old approved providers still require the full existing NINE-export
    // proof, with its own retained lifetime pin. No combined authority follows.
    if (!library) { library = managedPayloadCounterRuntimePin(); }
    if (!library) { return {}; }
    const auto capability = reinterpret_cast<decltype(&libtorrent::biglybtc_payload_counter_abi_v1)>(
        ::dlsym(library.get(), "biglybtc_payload_counter_abi_v1"));
    const auto read = reinterpret_cast<decltype(&libtorrent::biglybtc_read_payload_counters_v1)>(
        ::dlsym(library.get(), "biglybtc_read_payload_counters_v1"));
    const auto swarm = reinterpret_cast<decltype(&libtorrent::biglybtc_swarm_mask_abi_v1)>(
        ::dlsym(library.get(), "biglybtc_swarm_mask_abi_v1"));
    if (!capability || !read || !swarm || capability() != 1 || swarm() != 1
        || reinterpret_cast<void*>(capability) != ::dlsym(RTLD_DEFAULT, "biglybtc_payload_counter_abi_v1")
        || reinterpret_cast<void*>(read) != ::dlsym(RTLD_DEFAULT, "biglybtc_read_payload_counters_v1")
        || reinterpret_cast<void*>(swarm) != ::dlsym(RTLD_DEFAULT, "biglybtc_swarm_mask_abi_v1")) { return {}; }
    auto bridge = std::make_shared<ManagedPayloadCounterBridge>();
    bridge->library = std::move(library);bridge->read = read;bridge->swarm = swarm;
#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER
    bridge->attachQueue=attachQueue;
#endif
#if BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
    bridge->readTraffic = traffic;
#endif
    return bridge;
  } catch (...) { return {}; }
#else
  return {}; // Default/stock SDK: no private-class or guessed-zero fallback.
#endif
}

bool selectManagedSwarm(libtorrent::add_torrent_params& parameters,
    const storage::OriginalTorrentIdentity& identity,
    const std::shared_ptr<const ManagedPayloadCounterBridge>& bridge) noexcept {
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  if (!bridge || !bridge->library || !bridge->read || !bridge->swarm || !parameters.ti) { return false; }
  const auto full = parameters.ti->info_hashes();
  if (identity.full.v1.has_value() != full.has_v1() || identity.full.v2.has_value() != full.has_v2()) { return false; }
  if ((identity.full.v1 && !std::equal(identity.full.v1->begin(), identity.full.v1->end(),
           reinterpret_cast<const std::byte*>(full.v1.data())))
      || (identity.full.v2 && !std::equal(identity.full.v2->begin(), identity.full.v2->end(),
           reinterpret_cast<const std::byte*>(full.v2.data())))) { return false; }
  libtorrent::torrent_flags_t selected;
  switch (identity.selected) {
  case storage::OriginalSelectedHash::V1:
    if (!full.has_v1()) { return false; }
    selected = libtorrent::biglybtc_v1_only; break;
  case storage::OriginalSelectedHash::V2:
    if (!full.has_v2()) { return false; }
    selected = libtorrent::biglybtc_v2_only; break;
  default: return false;
  }
  parameters.flags = (parameters.flags & ~libtorrent::biglybtc_swarm_mask) | selected;
  return true;
#else
  static_cast<void>(parameters); static_cast<void>(identity); static_cast<void>(bridge);
  return false;
#endif
}

#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER
class ManagedQueueSink final:public libtorrent::BiglybtcQueueObserverV1 {
public:
  ManagedQueueSink(std::shared_ptr<ManagedQueueObservations> state,std::shared_ptr<const ManagedPayloadCounterBridge> bridge)
      :state_(std::move(state)),bridge_(std::move(bridge)){}
  void observe(const libtorrent::BiglybtcQueueEventV1& event) noexcept override {
    core::QueueObservation value;value.sequence=event.sequence;value.request=event.request;
    value.observedNanos=event.observed_nanoseconds;value.requestStartedNanos=event.request_started_nanoseconds;
    value.observedWallMillis=event.observed_wall_milliseconds;value.requestStartedWallMillis=event.request_started_wall_milliseconds;
    value.seeds=event.seeds;value.peers=event.peers;value.protocol=event.protocol;
    value.kind=static_cast<core::QueueObservationKind>(event.kind);value.tracker=event.tracker_sha256;state_->observe(value);
  }
private:
  std::shared_ptr<ManagedQueueObservations> state_;
  std::shared_ptr<const ManagedPayloadCounterBridge> bridge_; //same exact DSO remains pinned
};
#endif
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
class ManagedPayloadPlugin final : public libtorrent::torrent_plugin {
public:
  ManagedPayloadPlugin(libtorrent::torrent_handle handle,
      std::shared_ptr<ManagedPayloadCounters> counters,
      std::shared_ptr<const ManagedPayloadCounterBridge> bridge,
      std::shared_ptr<ManagedQueueObservations> queue)
      : handle_(std::move(handle)), counters_(std::move(counters)), bridge_(std::move(bridge)) {
    bool combined = false;
#if BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
    combined = bridge_ && bridge_->readTraffic;
#endif
    counters_->installed(combined);
#if BIGLYBTC_SUPPORTED_QUEUE_OBSERVER
    if(queue&&bridge_->attachQueue){
      try {
        std::shared_ptr<libtorrent::BiglybtcQueueObserverV1> sink=std::make_shared<ManagedQueueSink>(queue,bridge_);
        if(bridge_->attachQueue(&handle_,&sink)==static_cast<std::uint32_t>(libtorrent::BiglybtcQueueObserverStatusV1::Accepted)){queue->installed();}
      }catch(...){/* Optional queue facts never change transfer/storage success. */}
    }
#else
    static_cast<void>(queue);
#endif
  }
  bool on_pause() noexcept override { paused_ = true; return false; }
  bool on_resume() noexcept override {
    if (!paused_) { return false; }
    paused_ = false;
    const auto raw = read();
    if (!raw) { counters_->fail(); return false; }
    if (!raw->payload.files_checked && (raw->payload.received != 0 || raw->payload.sent != 0)) {
      counters_->fail(); return false;
    }
    counters_->seal(raw->payload.received, raw->payload.sent, raw->protocol);
    return false; // Never override standard LT pause/resume behavior.
  }
  void on_files_checked() noexcept override {
    const auto raw = read();
    if (!raw || raw->payload.received != 0 || raw->payload.sent != 0) { counters_->fail(); }
    else if (!raw->protocol) { counters_->combinedUnavailable(); }
  }
private:
  struct Raw {
    libtorrent::BiglybtcPayloadCountersV1 payload;
    std::optional<ManagedPayloadCounters::Protocol> protocol;
  };
  std::optional<Raw> read() const noexcept {
    if (!bridge_) { return {}; }
    Raw result{};
#if BIGLYBTC_SUPPORTED_TRAFFIC_COUNTERS
    if (bridge_->readTraffic) {
      libtorrent::BiglybtcTrafficCountersV2 value{};
      if (bridge_->readTraffic(&handle_, &value, sizeof(value))
          != static_cast<std::uint32_t>(libtorrent::BiglybtcTrafficCounterStatusV2::Accepted)) { return {}; }
      result.payload = value.payload;
      if (value.reserved == 0 && value.protocol_known == 1
          && value.protocol_received >= 0 && value.protocol_sent >= 0) {
        result.protocol = ManagedPayloadCounters::Protocol{value.protocol_received,value.protocol_sent};
      }
    } else
#endif
    {
      // Exactly ONE provider read at the existing callback: no V2+V1 reread.
      if (!bridge_->read || bridge_->read(&handle_, &result.payload, sizeof(result.payload))
          != static_cast<std::uint32_t>(libtorrent::BiglybtcPayloadCounterStatusV1::Accepted)) { return {}; }
    }
    const auto& value = result.payload;
    if (value.reserved != 0 || value.files_checked > 1 || value.received < 0 || value.sent < 0) { return {}; }
    return result;
  }
  libtorrent::torrent_handle handle_; // Public weak handle, no private pointer escapes.
  std::shared_ptr<ManagedPayloadCounters> counters_;
  std::shared_ptr<const ManagedPayloadCounterBridge> bridge_;
  bool paused_ = true;
};
#endif

bool attachManagedPayloadCounters(libtorrent::add_torrent_params& parameters,
    const std::shared_ptr<ManagedPayloadCounters>& counters,
    std::shared_ptr<const ManagedPayloadCounterBridge> bridge,
    std::shared_ptr<ManagedQueueObservations> queue) noexcept {
  if (!counters) { return false; }
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  if (!bridge || !bridge->read) { counters->fail(); return false; }
  try {
    parameters.extensions.push_back([counters, bridge = std::move(bridge),queue=std::move(queue)](
        const libtorrent::torrent_handle& handle, libtorrent::client_data_t)
        -> std::shared_ptr<libtorrent::torrent_plugin> {
      try { return std::make_shared<ManagedPayloadPlugin>(handle, counters, bridge,queue); }
      catch (...) { counters->fail(); return {}; }
    });
    return true;
  } catch (...) { counters->fail(); return false; }
#else
  static_cast<void>(parameters); static_cast<void>(bridge);static_cast<void>(queue);
  counters->fail(); return false;
#endif
}
} // namespace biglybtc::engine::detail
