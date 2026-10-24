#include "managed_payload_counters.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/version.hpp>

#include <utility>

namespace biglybtc::engine::detail {
static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "Managed raw reset boundaries require the reviewed LT2.0.12 source");
#if !defined(TORRENT_DISABLE_EXTENSIONS) && TORRENT_ABI_VERSION == 1 \
    && !TORRENT_USE_ASSERTS && !TORRENT_USE_INVARIANT_CHECKS \
    && TORRENT_USE_I2P == 1 && !defined(TORRENT_DISABLE_LOGGING)

class ManagedPayloadPlugin final : public libtorrent::torrent_plugin {
public:
  ManagedPayloadPlugin(libtorrent::torrent_handle handle, std::shared_ptr<ManagedPayloadCounters> counters)
      : handle_(std::move(handle)), counters_(std::move(counters)) { counters_->installed(); }
  bool on_pause() noexcept override { paused_ = true; return false; }
  bool on_resume() noexcept override {
    if (!paused_) { return false; } // Duplicate active resume does not reset m_stat.
    paused_ = false;
    try {
      // native_handle is weak. Its temporary strong pointer and raw stat copy
      // exist only during this LT callback, before the standard resume handler.
      const auto torrent = handle_.native_handle();
      if (!torrent) { counters_->fail(); return false; }
      const auto raw = torrent->statistics();
      if (!torrent->are_files_checked()
          && (raw.total_payload_download() != 0 || raw.total_payload_upload() != 0)) {
        counters_->fail(); return false; // No supported recheck after payload.
      }
      counters_->seal(raw.total_payload_download(), raw.total_payload_upload());
    } catch (...) { counters_->fail(); }
    return false; // Never override standard LT pause/resume behavior.
  }
  void on_files_checked() noexcept override {
    try {
      const auto torrent = handle_.native_handle();
      if (!torrent) { counters_->fail(); return; }
      const auto raw = torrent->statistics();
      // For qualified managed admission this is the initial check, before any
      // payload; files_checked subsequently starts its first announcing epoch.
      if (raw.total_payload_download() != 0 || raw.total_payload_upload() != 0) { counters_->fail(); }
    } catch (...) { counters_->fail(); }
  }
private:
  libtorrent::torrent_handle handle_; // Weak; never dereferenced by the worker.
  std::shared_ptr<ManagedPayloadCounters> counters_;
  bool paused_ = true;
};

bool attachManagedPayloadCounters(libtorrent::add_torrent_params& parameters,
    const std::shared_ptr<ManagedPayloadCounters>& counters) noexcept {
  try {
    parameters.extensions.push_back([counters](const libtorrent::torrent_handle& handle, libtorrent::client_data_t)
        -> std::shared_ptr<libtorrent::torrent_plugin> {
      try { return std::make_shared<ManagedPayloadPlugin>(handle, counters); }
      catch (...) { counters->fail(); return {}; }
    });
    return true;
  } catch (...) { counters->fail(); return false; }
}
#else
bool attachManagedPayloadCounters(libtorrent::add_torrent_params&,
    const std::shared_ptr<ManagedPayloadCounters>& counters) noexcept {
  counters->fail(); return false; // No ABI fallback or guessed zero counters.
}
#endif
} // namespace biglybtc::engine::detail
