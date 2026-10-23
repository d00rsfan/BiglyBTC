#include "peer_payload_observer.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_connection_interface.hpp>
#include <libtorrent/stat.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
#include <ratio>
#include <utility>

namespace biglybtc::engine::detail {
core::PeerNetworkClass classifyPeerPayloadEndpoint(const core::PeerNumericEndpoint& endpoint) noexcept {
  const auto& b=endpoint.address;bool local=false;
  if(endpoint.family==core::PeerAddressFamily::V4){
    local=b[0]==10||(b[0]==172&&(b[1]&0xf0)==16)||(b[0]==192&&b[1]==168)
        ||(b[0]==169&&b[1]==254)||b[0]==127||(b[0]==100&&(b[1]&0xc0)==64);
  }else if(endpoint.family==core::PeerAddressFamily::V6){
    const bool loopback=b[15]==1&&std::all_of(b.begin(),b.end()-1,[](std::uint8_t value){return value==0;});
    local=loopback||(b[0]==0xfe&&((b[1]&0xc0)==0x80||(b[1]&0xc0)==0xc0))
        ||(b[0]==0xff&&((b[1]&0x0f)==2||(b[1]&0x0f)==5))||(b[0]&0xfe)==0xfc;
  }else{return core::PeerNetworkClass::Unknown;}
  return local?core::PeerNetworkClass::Local:core::PeerNetworkClass::NonLocal;
}
namespace {
namespace lt = libtorrent;
static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "Passive peer statistics are source-bound to reviewed LT2.0.12");
// The approved imported Release provider uses these interface/layout macros.
// Other compile shapes omit the optional adapter, not essential transfer work.
#if !defined(TORRENT_DISABLE_EXTENSIONS) && TORRENT_ABI_VERSION == 1 \
    && !TORRENT_USE_ASSERTS && !TORRENT_USE_INVARIANT_CHECKS \
    && TORRENT_USE_I2P == 1 && !defined(TORRENT_DISABLE_LOGGING)
#define BIGLYBTC_PEER_PAYLOAD_SOURCE_ABI 1
#else
#define BIGLYBTC_PEER_PAYLOAD_SOURCE_ABI 0
#endif
#if BIGLYBTC_PEER_PAYLOAD_SOURCE_ABI
std::int64_t monotonicNow() noexcept {
  using Clock = std::chrono::steady_clock;
  static_assert(Clock::is_steady);
  // The qualified Linux clock uses nanoseconds. Reject rather than narrow a
  // larger platform representation; no wall-clock/alert timestamp is used.
  static_assert(std::ratio_equal_v<Clock::period, std::nano>);
  static_assert(std::numeric_limits<Clock::rep>::digits <= std::numeric_limits<std::int64_t>::digits);
  return static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
}
core::PeerNumericEndpoint numeric(const lt::tcp::endpoint& endpoint) {
  core::PeerNumericEndpoint result; result.port = endpoint.port();
  const auto address = endpoint.address();
  if (address.is_v4()) {
    result.family = core::PeerAddressFamily::V4;
    const auto bytes = address.to_v4().to_bytes(); std::copy(bytes.begin(), bytes.end(), result.address.begin());
  } else if (address.is_v6()) {
    result.family = core::PeerAddressFamily::V6;
    const auto v6 = address.to_v6(); const auto bytes = v6.to_bytes();
    std::copy(bytes.begin(), bytes.end(), result.address.begin()); result.scope = v6.scope_id();
  }
  return result;
}

class PeerObserver final : public lt::peer_plugin {
public:
  PeerObserver(std::shared_ptr<core::PeerPayloadLedger> ledger, lt::peer_connection_handle peer,
      std::uint64_t token) noexcept : ledger_(std::move(ledger)), peer_(std::move(peer)), token_(token) {}
  ~PeerObserver() override { ledger_->released(token_); } // No peer read here.
  void on_connected() noexcept override {
    ledger_->connected(token_, monotonicNow()); sample(false);
  }
  void tick() noexcept override { sample(false); }
  void on_disconnect(const lt::error_code&) noexcept override { sample(true); }
private:
  void sample(bool disconnect) noexcept {
    const auto now = monotonicNow();
    std::optional<core::PeerPayloadSample> value;
    try {
      // Temporary strong reference exists ONLY inside an LT callback. No
      // pointer/reference crosses the mailbox or survives to the next turn.
      if (const auto peer = peer_.native_handle()) {
        // Virtual accessor avoids copying peer_info::pieces. Both interface
        // and stat layout are source/ABI-bound, not a general public API claim.
        const lt::peer_connection_interface& view = *peer;
        const auto& statistics = view.statistics();
        value = core::PeerPayloadSample{statistics.total_payload_download(), statistics.total_payload_upload(),
            statistics.download_payload_rate(), statistics.upload_payload_rate()};
      }
    } catch (...) { ledger_->noteLoss(core::PeerPayloadIssue::SampleUnavailable); }
    ledger_->observe(token_, now, value, disconnect);
  }
  std::shared_ptr<core::PeerPayloadLedger> ledger_;
  lt::peer_connection_handle peer_; // A weak handle; never used from the worker.
  std::uint64_t token_;
};
class TorrentObserver final : public lt::torrent_plugin {
public:
  TorrentObserver(std::shared_ptr<core::PeerPayloadLedger> ledger, std::uint64_t operation) noexcept
      : ledger_(std::move(ledger)), operation_(operation) {}
  std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle& handle) noexcept override {
    try {
      const auto peer = handle.native_handle();
      if (!peer) { ledger_->noteLoss(core::PeerPayloadIssue::SampleUnavailable); return {}; }
      if (handle.type() != lt::connection_type::bittorrent) {
        ledger_->noteLoss(core::PeerPayloadIssue::UnsupportedConnection); return {};
      }
      const auto endpoint = numeric(handle.remote());
      const auto token = ledger_->attach(operation_, endpoint, classifyPeerPayloadEndpoint(endpoint),
          monotonicNow());
      if (!token) { return {}; }
      try { return std::make_shared<PeerObserver>(ledger_, handle, token); }
      catch (...) { ledger_->noteLoss(core::PeerPayloadIssue::Factory); ledger_->released(token); return {}; }
    } catch (...) { ledger_->noteLoss(core::PeerPayloadIssue::Factory); return {}; }
  }
private:
  std::shared_ptr<core::PeerPayloadLedger> ledger_;
  std::uint64_t operation_;
};
#endif
} // namespace

void attachPeerPayloadObserver(lt::add_torrent_params& parameters,
    const std::shared_ptr<core::PeerPayloadLedger>& ledger, std::uint64_t operation) noexcept {
  if (!ledger) { return; }
#if BIGLYBTC_PEER_PAYLOAD_SOURCE_ABI
  try {
    parameters.extensions.push_back([ledger, operation](const lt::torrent_handle&, lt::client_data_t) noexcept
        -> std::shared_ptr<lt::torrent_plugin> {
      try { return std::make_shared<TorrentObserver>(ledger, operation); }
      catch (...) { ledger->noteLoss(core::PeerPayloadIssue::Factory); return {}; }
    });
  } catch (...) { ledger->noteLoss(core::PeerPayloadIssue::Factory); }
#else
  static_cast<void>(parameters); static_cast<void>(operation);
  ledger->noteLoss(core::PeerPayloadIssue::UnsupportedAbi);
#endif
}
} // namespace biglybtc::engine::detail
