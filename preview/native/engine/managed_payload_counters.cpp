#include "managed_payload_counters.hpp"
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

namespace biglybtc::engine::detail {
static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "Managed raw reset boundaries require the reviewed LT2.0.12 source");

struct ManagedPayloadCounterBridge final {
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  std::shared_ptr<void> library;
  decltype(&libtorrent::biglybtc_read_payload_counters_v1) read = nullptr;
  decltype(&libtorrent::biglybtc_swarm_mask_abi_v1) swarm = nullptr;
#endif
};

std::shared_ptr<const ManagedPayloadCounterBridge> managedPayloadCounterBridge() noexcept {
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  try {
    // The complete source/artifact/mapping proof covers all NINE exports.
    // Retain its exact module handle, with no proof-to-reopen lifetime gap.
    // BOUND0 returns before any loader call. No caller supplies a trust token.
    auto library = managedPayloadCounterRuntimePin();
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
    return std::make_shared<const ManagedPayloadCounterBridge>(ManagedPayloadCounterBridge{std::move(library), read, swarm});
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

#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
class ManagedPayloadPlugin final : public libtorrent::torrent_plugin {
public:
  ManagedPayloadPlugin(libtorrent::torrent_handle handle,
      std::shared_ptr<ManagedPayloadCounters> counters,
      std::shared_ptr<const ManagedPayloadCounterBridge> bridge)
      : handle_(std::move(handle)), counters_(std::move(counters)), bridge_(std::move(bridge)) {
    counters_->installed();
  }
  bool on_pause() noexcept override { paused_ = true; return false; }
  bool on_resume() noexcept override {
    if (!paused_) { return false; } // Duplicate active resume does not reset m_stat.
    paused_ = false;
    const auto raw = read();
    if (!raw) { counters_->fail(); return false; }
    if (!raw->files_checked && (raw->received != 0 || raw->sent != 0)) {
      counters_->fail(); return false; // No supported recheck after payload.
    }
    counters_->seal(raw->received, raw->sent);
    return false; // Never override standard LT pause/resume behavior.
  }
  void on_files_checked() noexcept override {
    const auto raw = read();
    // Initial check precedes the first announcing epoch, exactly as before.
    if (!raw || raw->received != 0 || raw->sent != 0) { counters_->fail(); }
  }
private:
  std::optional<libtorrent::BiglybtcPayloadCountersV1> read() const noexcept {
    libtorrent::BiglybtcPayloadCountersV1 value{};
    if (!bridge_ || !bridge_->read
        || bridge_->read(&handle_, &value, sizeof(value))
            != static_cast<std::uint32_t>(libtorrent::BiglybtcPayloadCounterStatusV1::Accepted)
        || value.reserved != 0 || value.files_checked > 1 || value.received < 0 || value.sent < 0) { return {}; }
    return value;
  }
  libtorrent::torrent_handle handle_; // Public weak handle, never dereferenced here.
  std::shared_ptr<ManagedPayloadCounters> counters_;
  std::shared_ptr<const ManagedPayloadCounterBridge> bridge_;
  bool paused_ = true;
};
#endif

bool attachManagedPayloadCounters(libtorrent::add_torrent_params& parameters,
    const std::shared_ptr<ManagedPayloadCounters>& counters,
    std::shared_ptr<const ManagedPayloadCounterBridge> bridge) noexcept {
  if (!counters) { return false; }
#if BIGLYBTC_SUPPORTED_PAYLOAD_COUNTERS
  if (!bridge || !bridge->read) { counters->fail(); return false; }
  try {
    parameters.extensions.push_back([counters, bridge = std::move(bridge)](
        const libtorrent::torrent_handle& handle, libtorrent::client_data_t)
        -> std::shared_ptr<libtorrent::torrent_plugin> {
      try { return std::make_shared<ManagedPayloadPlugin>(handle, counters, bridge); }
      catch (...) { counters->fail(); return {}; }
    });
    return true;
  } catch (...) { counters->fail(); return false; }
#else
  static_cast<void>(parameters); static_cast<void>(bridge);
  counters->fail(); return false;
#endif
}
} // namespace biglybtc::engine::detail
