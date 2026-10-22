#include "overall_statistics_service.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace biglybtc;
std::size_t assertions = 0;
void require(bool condition, std::string_view message) {
  ++assertions;
  if (!condition) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { if (value >= 0) { ::close(value); } }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
void write(const std::filesystem::path& path, std::string_view bytes) {
  Fd file(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600));
  require(file.value >= 0, "open owned service fixture");
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR) { continue; }
    require(n > 0, "write owned service fixture"); offset += static_cast<std::size_t>(n);
  }
}
std::string read(const std::filesystem::path& path) {
  Fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  require(file.value >= 0, "read owned service fixture");
  std::string result; std::array<char, 4096> buffer{};
  for (;;) {
    const auto n = ::read(file.value, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR) { continue; }
    require(n >= 0, "read owned service bytes");
    if (!n) { return result; }
    result.append(buffer.data(), static_cast<std::size_t>(n));
  }
}
struct Area {
  std::filesystem::path root;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-overall-service-tests-XXXXXX";
    const auto* created = ::mkdtemp(pattern); require(created != nullptr, "create private service fixture"); root = created;
    auto held = storage::ProfileLease::acquire(root); require(held.has_value(), "hold service fixture lease");
    lease.emplace(std::move(*held));
  }
  ~Area() { lease.reset(); std::error_code ignored; std::filesystem::remove_all(root, ignored); }
  std::filesystem::path primary() const { return root / "BiglyBT.statistics"; }
  std::filesystem::path backup() const { return root / "BiglyBT.statistics.bak"; }
  std::filesystem::path saving() const { return root / "BiglyBT.statistics.saving"; }
};
constexpr app::OverallOwnerIdentity Identity{1, 7, 1};
core::OverallDhtInput disabledDht() { return {core::OverallDhtResolution::Empty, {}}; }
std::unique_ptr<app::OverallStatisticsService> open(Area& area) {
  auto service = app::OverallStatisticsService::open(*area.lease, Identity, 0, 0);
  require(service.has_value(), "open explicit service domain"); return std::move(*service);
}
app::OverallOwnerSample sample(std::uint64_t sequence = 1) {
  return {Identity, sequence, {11,13,17,19}, app::OverallSampleQuality::Observed};
}
void cadenceAndOwnedBaseline() {
  Area area; auto service = open(area);
  for (std::uint64_t i = 1; i <= 1000; ++i) { require(service->capture(sample(i)).has_value(), "retain fixed actual-input primitives"); }
  require(service->values()[0] == 0 && !service->dirty() && !service->pendingValues(), "capture does not run reducer or timer");
  for (int tick = 1; tick <= 10; ++tick) {
    auto result = service->tick(static_cast<std::int64_t>(tick) * 60000, disabledDht(), false);
    require(result && result->disposition == core::OverallUpdateDisposition::Accepted, "explicit one-minute source tick");
    require(result->save.has_value() == (tick == 10), "source ten-tick cadence independent of1000 observations");
    if (tick < 10) { require(service->flush() == app::OverallFlushDisposition::NothingPending, "no premature transaction"); }
  }
  require(service->values()[0] == 24 && service->values()[2] == 600 && service->dirty(), "peer absolute counted once and uptime advanced");
  auto saved = service->flush();
  require(saved && *saved == app::OverallFlushDisposition::Committed && !service->dirty(), "tenth tick first creation commits");
  require(service->baseline().snapshot->values == service->values() && !service->pendingValues() && !service->pendingDocument(), "committed baseline is owned and pending cleared");
}
void marksAndOpaqueFields() {
  Area area;
  const std::string opaque("\0\xff", 2);
  const auto original = std::string("d4:tail2:") + opaque + "3:alld10:downloadedi100e8:uploadedi20e6:uptimei10eee";
  write(area.primary(), original); auto service = open(area);
  require(service->capture(sample()).has_value(), "mark input sample");
  require(service->tick(60000, disabledDht(), true).has_value(), "stage force before mark without intermediate IO");
  auto marked = service->setMark(60777);
  require(marked && *marked == app::OverallFlushDisposition::Committed && read(area.backup()) == original,
      "one marked transaction, without intermediate pre-mark backup");
  auto doc = storage::readOverallStatisticsView(*area.lease);
  require(doc && doc->snapshot->values[3] == 60777 && doc->snapshot->values[4] == 124 && doc->snapshot->values[6] == 70,
      "mark stores exact current totals/time");
  require(doc->snapshot->source->find("tail")->bytes == opaque, "mark retains unknown raw bytes");
  const auto markedBytes = read(area.primary());
  auto cleared = service->clearMark();
  require(cleared && read(area.backup()) == markedBytes && service->values()[3] == 0 && service->values()[4] == 0
      && service->values()[0] == 124, "clear mark immediately saves and preserves totals");
}
void actualErrorFreezesDesiredAndBlocksRetry() {
  Area area; write(area.primary(), "de");
  require(::mkdir(area.backup().c_str(), 0700) == 0, "incompatible owned backup pathname");
  auto service = open(area); require(service->capture(sample()).has_value(), "error input capture");
  require(service->tick(60000, disabledDht(), true).has_value(), "error force staging");
  auto failed = service->flush();
  require(!failed && failed.error() == app::OverallServiceError::Storage && service->writesBlocked()
      && service->lastStorageError() && service->lastStorageError()->code == storage::ErrorCode::Io,
      "actual backup rename error retained and writer latched");
  const auto errorText = service->lastStorageError()->message;
  require(errorText.find("Backup transition") != std::string::npos, "underlying diagnostic verbatim");
  require(service->pendingDocument() && service->pendingValues(), "frozen absolute document and values survive write error");
  const auto frozen = service->pendingDocument()->source->originalBytes();
  const auto values = *service->pendingValues();
  require(::rmdir(area.backup().c_str()) == 0 && ::unlink(area.saving().c_str()) == 0, "fixture removes error obstacle explicitly");
  auto newer = sample(2); newer.absolute.dataReceived = 21;
  require(service->capture(newer).has_value() && service->tick(120000, disabledDht(), true).has_value(), "live accounting continues despite IO failure");
  auto again = service->flush();
  require(!again && again.error() == app::OverallServiceError::WriteBlocked && read(area.primary()) == "de"
      && !std::filesystem::exists(area.saving()), "no automatic retry even after filesystem obstacle gone");
  require(*service->pendingValues() == values && service->pendingDocument()->source->originalBytes() == frozen
      && service->lastStorageError()->message == errorText && service->values()[0] == 34 && service->dirty(),
      "new live totals cannot replace failed desired or original error");
}
void singleEpochAndFinalReceiptPolicy() {
  // Synthetic caller-quality policy tests; no real engine certification claim.
  Area area; auto service = open(area); auto first = sample(10);
  require(service->capture(first).has_value(), "accept current identity");
  auto wrong = sample(11); ++wrong.identity.engineEpoch;
  require(!service->capture(wrong) && service->latestSample() == first, "epoch transition rejected without cumulative folding");
  require(!service->capture(first), "duplicate observed sequence is stale");
  auto regression = sample(11); regression.absolute.dataReceived = 1;
  auto rejected = service->capture(regression);
  require(!rejected && rejected.error() == app::OverallServiceError::CounterRegression && service->latestSample() == first, "regression cannot mutate fixed sample");
  first.quality = app::OverallSampleQuality::SettledCheckpoint;
  require(service->capture(first).has_value(), "same exact checkpoint can promote quality");
  first.quality = app::OverallSampleQuality::ClosedFinal;
  require(service->capture(first).has_value() && service->capture(first).has_value(), "same exact genuine-final input is idempotent");
  require(!service->tick(60000, disabledDht(), false), "closed input requires explicit force");
  auto force = service->tick(101000, disabledDht(), true);
  require(force && force->disposition == core::OverallUpdateDisposition::ClockGap && !force->save
      && service->finalCountersApplied() && service->values()[0] == 24 && service->values()[2] == 0,
      "final counters apply before source clock-gap return without invented uptime or save intention");
  require(service->saveCurrent().has_value(), "explicit save current does not recalculate final delta");
  require(!service->tick(120000, disabledDht(), true) && !service->capture(sample(12)), "closed sample cannot continue application uptime or admit next sample");
}
void cancellationRecoveryAndUncertified() {
  Area area; auto service = open(area);
  require(!service->tick(60000, disabledDht(), true), "no fabricated sample before first actual capture");
  require(service->capture(sample()).has_value() && service->tick(60000, disabledDht(), true).has_value(), "prepare cancellable intent");
  std::stop_source stopped; stopped.request_stop(); auto cancelled = service->flush(stopped.get_token());
  require(!cancelled && cancelled.error() == app::OverallServiceError::Cancelled && !service->writesBlocked()
      && service->pendingValues() && !std::filesystem::exists(area.primary()), "pretransaction cancellation preserves intent without uncertainty");
  require(service->flush().has_value(), "pretransaction cancellation permits deliberate later flush");
  require(service->invalidate(Identity).has_value() && service->uncertified(), "owner uncertainty retained independently of saved prefix");
  require(!service->capture(sample(2)) && !service->tick(120000, disabledDht(), true), "uncertified tail cannot grow lifetime totals");
  Area recovered; write(recovered.backup(), "d3:alld10:downloadedi7eee");
  auto recovery = open(recovered);
  require(recovery->recoveredReadOnly() && recovery->writesBlocked() && recovery->lastStorageError()
      && recovery->values()[0] == 7, "loaded recovery disables writer without blocking domain availability");
  require(recovery->capture(sample()).has_value() && recovery->tick(60000, disabledDht(), true).has_value(), "recovery read-only policy does not imply transfer failure");
  require(!recovery->flush() && !std::filesystem::exists(recovered.primary()) && read(recovered.backup()) == "d3:alld10:downloadedi7eee", "no automatic recovery rewrite");
}
void projectionUsesActualCounterBaselinesAfterClockGap() {
  Area area; auto service = open(area);
  require(service->capture(sample()).has_value(), "projection input");
  require(service->projectedValues()[0] == 24 && service->values()[0] == 0,
      "getter projects unflushed peer bytes without mutating reducer");
  auto gap = service->tick(101000, disabledDht(), true);
  require(gap && gap->disposition == core::OverallUpdateDisposition::ClockGap
      && service->values()[0] == 24 && service->observation().snapshot[9] == 0,
      "source gap has advanced actual peer baseline but deliberately stale snapshot");
  require(service->projectedValues()[0] == 24, "clock gap interval is not counted twice in live projection");
  auto newer = sample(2); newer.absolute.dataReceived = 21;
  require(service->capture(newer).has_value() && service->projectedValues()[0] == 34
      && service->values()[0] == 24, "only later actual delta is projected after gap");
  require(service->tick(102000, disabledDht(), true).has_value() && service->projectedValues()[0] == 34,
      "accepted update and projection agree after the next actual sample");
  Area overflow; write(overflow.primary(), "d3:alld10:downloadedi9223372036854775807e8:uploadedi-1eee");
  auto edge = open(overflow); require(edge->capture(sample()).has_value(), "projection overflow input");
  const auto projected = edge->projectedValues();
  require(projected[0] == 0 && projected[1] == 35 && edge->values()[1] == -1,
      "projection implements Java wrap/clamp without modifying loaded totals");
}
void wrongThreadAndStrictInput() {
  Area area; auto service = open(area);
  std::optional<app::OverallServiceError> wrongThread;
  std::thread other([&] { auto r = service->capture(sample()); if (!r) { wrongThread = r.error(); } }); other.join();
  require(wrongThread == app::OverallServiceError::WrongThread && !service->latestSample(), "worker affinity rejects before mutation");
  auto invalid = sample(); invalid.absolute.protocolReceived = -1;
  require(!service->capture(invalid) && !service->latestSample(), "negative actual counters rejected");
  invalid = sample(); invalid.quality = static_cast<app::OverallSampleQuality>(99);
  require(!service->capture(invalid), "invalid sample quality rejected");
  require(service->capture(sample()).has_value(), "valid caller input after rejects");
  const auto before = service->values();
  auto badDht = service->tick(60000, {static_cast<core::OverallDhtResolution>(99), {}}, true);
  require(!badDht && service->values() == before, "explicit invalid DHT state cannot become implicit zero");
}
} // namespace
int main() {
  try {
    cadenceAndOwnedBaseline(); marksAndOpaqueFields(); actualErrorFreezesDesiredAndBlocksRetry();
    singleEpochAndFinalReceiptPolicy(); cancellationRecoveryAndUncertified(); projectionUsesActualCounterBaselinesAfterClockGap(); wrongThreadAndStrictInput();
    std::cout << "PASS overall-statistics-service: 7 families; assertions=" << assertions << '\n'; return 0;
  } catch (const std::exception& failure) { std::cerr << "FAIL overall-statistics-service: " << failure.what() << '\n'; return 1; }
}
