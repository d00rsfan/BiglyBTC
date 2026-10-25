#include "managed_payload_counters.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <libtorrent/biglybtc_payload_counters.hpp>
#include <libtorrent/extensions.hpp>

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using namespace std::chrono_literals;
using Raw = lt::BiglybtcPayloadCountersV1;
using Result = lt::BiglybtcPayloadCounterStatusV1;
using Counters = engine::detail::ManagedPayloadCounters;

constexpr std::uint32_t code(Result value) { return static_cast<std::uint32_t>(value); }
bool equal(const Raw& a, const Raw& b) {
  return a.received == b.received && a.sent == b.sent
      && a.files_checked == b.files_checked && a.reserved == b.reserved;
}
std::shared_ptr<lt::torrent_info> metadata(const test::OwnedEngineFixture& fixture) {
  const auto bytes = test::read(fixture.stateFile);
  lt::error_code error;
  auto value = std::make_shared<lt::torrent_info>(
      lt::span<const char>(bytes.constData(), bytes.size()), error, lt::from_span);
  test::require(!error, "Generated provider metadata was rejected");
  value->clear_trackers();
  return value;
}
lt::add_torrent_params parameters(const test::OwnedEngineFixture& fixture) {
  lt::add_torrent_params value;
  value.ti = metadata(fixture); value.save_path = fixture.payload.toStdString();
  value.flags |= lt::torrent_flags::paused | lt::torrent_flags::disable_dht
      | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
  value.flags &= ~lt::torrent_flags::auto_managed;
  value.download_limit = 32768;
  return value;
}

struct Observation {
  bool resume = false;
  Raw raw{};
  std::int64_t statusReceived = 0, statusSent = 0;
};
struct Observations {
  std::mutex mutex;
  std::vector<Observation> values;
  std::atomic_bool failed{false};
  std::vector<Observation> copy() {
    std::lock_guard lock(mutex); return values;
  }
  std::size_t resumes() {
    const auto all = copy();
    return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [](const auto& v) { return v.resume; }));
  }
};
// This observes the real public callback order. No alert wrapper, private
// torrent class, injected counter values or extra alert consumer is used.
class Observer final : public lt::torrent_plugin {
public:
  Observer(lt::torrent_handle handle, std::shared_ptr<Observations> state)
      : handle_(std::move(handle)), state_(std::move(state)) {}
  bool on_resume() noexcept override { observe(true); return false; }
  void on_files_checked() noexcept override { observe(false); }
private:
  void observe(bool resume) noexcept {
    try {
      Raw raw{};
      if (lt::biglybtc_read_payload_counters_v1(&handle_, &raw, sizeof(raw)) != code(Result::Accepted)) {
        state_->failed.store(true); return;
      }
      // status() executes inline on this same executor thread; no IO event can
      // occur between the two reads. Both source paths read exactly m_stat.
      const auto status = handle_.status();
      std::lock_guard lock(state_->mutex);
      if (state_->values.size() == 32) { state_->failed.store(true); return; }
      state_->values.push_back({resume, raw, status.total_payload_download, status.total_payload_upload});
    } catch (...) { state_->failed.store(true); }
  }
  lt::torrent_handle handle_; // weak
  std::shared_ptr<Observations> state_;
};

std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& owner, const core::TorrentHash& hash) {
  const auto values = owner.managedDownloads();
  const auto found = std::find_if(values.begin(), values.end(), [&](const auto& v) { return v.hash == hash; });
  return found == values.end() ? std::nullopt : std::optional{*found};
}
bool isPhase(engine::PersistentSession& owner, const core::TorrentHash& hash, engine::ManagedPhase phase) {
  const auto value = status(owner, hash); return value && !value->failure && value->phase == phase;
}
} // namespace

class ProviderPayloadCounterTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    // This target is a POSITIVE provider qualification lane. Missing capability
    // or unapproved artifact fails; it never skips or silently tests a fallback.
    QCOMPARE(lt::biglybtc_payload_counter_abi_v1(), 1u);
    QVERIFY2(engine::detail::managedPayloadCounterBridge(), "Requires the newly approved exact provider and SDK");
  }
  void invalidAndWrongThreadReadsDoNotTouchOutputOrDispatch() {
    const Raw sentinel{-17, -23, 47, 91};
    Raw output = sentinel;
    lt::torrent_handle empty;
    QCOMPARE(lt::biglybtc_read_payload_counters_v1(nullptr, &output, sizeof(output)), code(Result::InvalidArgument));
    QVERIFY(equal(output, sentinel));
    QCOMPARE(lt::biglybtc_read_payload_counters_v1(&empty, nullptr, sizeof(output)), code(Result::InvalidArgument));
    QCOMPARE(lt::biglybtc_read_payload_counters_v1(&empty, &output, sizeof(output) - 1), code(Result::InvalidArgument));
    QVERIFY(equal(output, sentinel));
    QCOMPARE(lt::biglybtc_read_payload_counters_v1(&empty, &output, sizeof(output)), code(Result::InvalidHandle));
    QVERIFY(equal(output, sentinel));
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    lt::torrent_handle handle;
    {
      lt::session session(test::localSettings());
      lt::error_code error;
      handle = session.add_torrent(parameters(fixture), error);
      QVERIFY(!error && handle.is_valid());
      QCOMPARE(lt::biglybtc_read_payload_counters_v1(&handle, &output, sizeof(output)), code(Result::WrongThread));
      QVERIFY(equal(output, sentinel));
      QVERIFY(handle.status().flags & lt::torrent_flags::paused);
    } // Real session abort/join; the public handle is weak.
    QCOMPARE(lt::biglybtc_read_payload_counters_v1(&handle, &output, sizeof(output)), code(Result::InvalidHandle));
    QVERIFY(equal(output, sentinel));
    QVERIFY(fixture.intact(false));
  }
  void actualCallbackCopiesMatchStatusAndCarryThePreClearEpoch() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto bridge = engine::detail::managedPayloadCounterBridge(); QVERIFY(bridge);
    const auto counters = std::make_shared<Counters>();
    const auto observations = std::make_shared<Observations>();
    lt::session session(test::localSettings());
    auto options = parameters(fixture);
    QVERIFY(engine::detail::attachManagedPayloadCounters(options, counters, bridge));
    options.extensions.push_back([observations](const lt::torrent_handle& handle, lt::client_data_t)
        -> std::shared_ptr<lt::torrent_plugin> { return std::make_shared<Observer>(handle, observations); });
    lt::error_code error;
    const auto handle = session.add_torrent(std::move(options), error);
    QVERIFY(!error && handle.is_valid());
    bridge.reset(); // The installed factory/plugin must retain its module pin.
    handle.resume();
    QTRY_VERIFY_WITH_TIMEOUT(handle.status().state == lt::torrent_status::downloading, 10000);
    handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(handle.status().total_payload_download > 0, 30000);
    handle.pause();
    QTRY_VERIFY_WITH_TIMEOUT(bool(handle.status().flags & lt::torrent_flags::paused), 10000);
    const auto paused = handle.status();
    QVERIFY(paused.total_payload_download > 0);
    const auto before = counters->epoch(); QVERIFY(before);
    const auto accepted = counters->combine(*before, paused.total_payload_download, paused.total_payload_upload);
    QCOMPARE(accepted.state, Counters::Read::Accepted);
    QCOMPARE(accepted.received, paused.total_payload_download);
    const auto oldResumes = observations->resumes();
    handle.resume();
    QTRY_VERIFY_WITH_TIMEOUT(observations->resumes() == oldResumes + 1, 10000);
    // An actual public status call ordered after resume observes the post-clear
    // epoch. Reusing the old epoch must request another read, never add guesses.
    const auto after = handle.status();
    QCOMPARE(counters->combine(*before, after.total_payload_download, after.total_payload_upload).state, Counters::Read::Retry);
    const auto epoch = counters->epoch(); QVERIFY(epoch);
    const auto combined = counters->combine(*epoch, after.total_payload_download, after.total_payload_upload);
    QCOMPARE(combined.state, Counters::Read::Accepted);
    QVERIFY(combined.received >= accepted.received && combined.sent >= accepted.sent);
    const auto values = observations->copy();
    QVERIFY(!observations->failed.load());
    QVERIFY(std::any_of(values.begin(), values.end(), [](const auto& v) { return !v.resume; }));
    const auto last = std::find_if(values.rbegin(), values.rend(), [](const auto& v) { return v.resume; });
    QVERIFY(last != values.rend());
    // A completion already pending at pause may add real tail bytes before
    // resume. Its final callback sample, rather than a prior status prefix,
    // is the exact base that must be retained across LT's reset.
    QVERIFY(last->raw.received >= paused.total_payload_download);
    QVERIFY(last->raw.sent >= paused.total_payload_upload);
    QCOMPARE(combined.received, last->raw.received + after.total_payload_download);
    QCOMPARE(combined.sent, last->raw.sent + after.total_payload_upload);
    QCOMPARE(last->raw.files_checked, std::uint32_t{1});
    for (const auto& value : values) {
      QCOMPARE(value.raw.received, value.statusReceived); QCOMPARE(value.raw.sent, value.statusSent);
      QCOMPARE(value.raw.reserved, std::uint32_t{0});
    }
    // A duplicate active resume does not create a new reset epoch.
    handle.resume(); static_cast<void>(handle.status());
    const auto duplicate = counters->epoch(); QVERIFY(duplicate);
    QCOMPARE(duplicate->number, epoch->number);
    QVERIFY(fixture.intact(false));
  }
  void actualManagedPauseAndRetirementKeepCheckpointAccounting() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    auto created = engine::PersistentSession::create({901, 902, 903}); QVERIFY(created);
    auto owner = std::move(*created);
    QVERIFY(owner->startManaged(*lease, {.hash = *fixture.hash, .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value() && status(*owner, *fixture.hash)
        && status(*owner, *fixture.hash)->received > 0 && !status(*owner, *fixture.hash)->failure, 30000);
    QVERIFY(owner->pauseManaged(*fixture.hash, true));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value() && isPhase(*owner, *fixture.hash, engine::ManagedPhase::Paused), 10000);
    const auto paused = status(*owner, *fixture.hash); QVERIFY(paused);
    QVERIFY(owner->pauseManaged(*fixture.hash, false));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value() && isPhase(*owner, *fixture.hash, engine::ManagedPhase::Downloading), 10000);
    const auto resumed = status(*owner, *fixture.hash); QVERIFY(resumed);
    QVERIFY(resumed->received >= paused->received && resumed->sent >= paused->sent);
    QVERIFY(owner->stopManaged(*fixture.hash));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value() && isPhase(*owner, *fixture.hash, engine::ManagedPhase::Stopped), 10000);
    const auto stopped = status(*owner, *fixture.hash); QVERIFY(stopped && stopped->received >= resumed->received);
    const auto raw = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    auto inventory = core::Document::parse(raw.toStdString(), storage::compatibleConfigLimits()); QVERIFY(inventory);
    const auto* downloads = inventory->find("downloads"); QVERIFY(downloads && downloads->children.size() == 1);
    const auto& row = downloads->children.front();
    QVERIFY(row.find("downloaded") && row.find("uploaded"));
    QCOMPARE(row.find("downloaded")->integer, stopped->received);
    QCOMPARE(row.find("uploaded")->integer, stopped->sent);
    const auto closed = owner->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);
    QVERIFY(fixture.intact(false));
  }
};
QTEST_GUILESS_MAIN(ProviderPayloadCounterTests)
#include "provider_payload_counter_tests.moc"
