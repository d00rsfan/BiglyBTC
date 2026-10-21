#pragma once

#include "local_session.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>

namespace biglybtc::engine {

struct AccountingIdentity {
  std::uint64_t applicationRun = 0;
  std::uint64_t profileSession = 0;
  std::uint64_t engineEpoch = 0;
  bool operator==(const AccountingIdentity&) const = default;
};
enum class AccountingState { Starting, Idle, Active, Retiring, Closing, Closed, Uncertified };
enum class AccountingError {
  InvalidIdentity, WrongThread, ActiveOperation, Closing, Deadline,
  CounterMapping, CounterResponseLost, CounterInvalid, CounterRegression,
  StorageFenceLost, OwnerResetRequired, Memory, Engine, DependencyUnqualified
};
struct ApplicationPeerObservation {
  AccountingIdentity identity;
  std::uint64_t sequence = 0;
  AccountingState state = AccountingState::Starting;
  // All measurements from the SAME persistent LT session. No operation sums.
  std::optional<core::EngineStatistics> observed;
  // Actual owner lifetime, separate from EngineStatistics first-observation time.
  std::int64_t ownerElapsedNanoseconds = 0;
};

using ApplicationObservationSink = std::function<void(ApplicationPeerObservation)>;

namespace detail { class PersistentCall; class PersistentOperation; }
class PersistentSession;
// Only the source-bound successful retirement protocol may mint a settled
// checkpoint. It is NOT epoch-final while a later admission is possible.
// Copying a receipt cannot turn a last-observed tail into settled evidence.
class SettledPeerCounters final {
public:
  const AccountingIdentity& identity() const noexcept { return identity_; }
  const core::EngineCounters& counters() const noexcept { return counters_; }
  std::uint64_t sequence() const noexcept { return sequence_; }
private:
  friend class PersistentSession;
  SettledPeerCounters(AccountingIdentity, core::EngineCounters, std::uint64_t) noexcept;
  AccountingIdentity identity_;
  core::EngineCounters counters_;
  std::uint64_t sequence_;
};
struct RetirementReceipt {
  // Nonzero admission attempt, including repeats of the same info hash.
  std::uint64_t operation = 0;
  // A lifetime/retirement barrier, NOT partfile metadata durability. Existing
  // explicit payload fsync and compatible state publication remain mandatory.
  bool storageReleased = false;
  std::optional<SettledPeerCounters> peerCounters;
  // No value means no diagnostics failure; presence never fabricates a seal.
  std::optional<AccountingError> accountingFailure;
};
struct CloseReceipt {
  RetirementReceipt retirement;
  // Epoch-final evidence requires this successful explicit close (permanently
  // closed admission gate), joined==true, storageReleased and peerCounters.
  bool sessionJoined = false;
};

// Confined to the existing Host executor's worker thread from creation through
// destruction. The service outlives individual continuation operations. The
// initial implementation admits at most ONE torrent at a time, reusing the same
// LT session and absolute counters across successes, cancellations and failures.
// No raw session/handle is exposed to the UI or callers; all pop_alerts go through
// one internal pump. No original profile or new public network is implied.
class PersistentSession final {
public:
  // Default builds reject reuse before creating any LT session. Only a private
  // reviewed source/runtime binding enables the pending qualification path.
  static std::expected<std::unique_ptr<PersistentSession>, AccountingError> create(AccountingIdentity);
  ~PersistentSession(); // Fallback abort+join only; never promises an exact seal.
  PersistentSession(const PersistentSession&) = delete;
  PersistentSession& operator=(const PersistentSession&) = delete;
  PersistentSession(PersistentSession&&) = delete;
  PersistentSession& operator=(PersistentSession&&) = delete;

  // Stable owner-level observation sink/mailbox is connected outside the ctor.
  // UI callback failure/coalescing must not disable counter retention or sealing.
  std::expected<void, AccountingError> setObservationSink(ApplicationObservationSink);
  void clearObservationSink() noexcept;
  std::optional<ApplicationPeerObservation> latest() const;
  // Cleared before each admitted call, so failed preparation cannot expose an
  // earlier operation receipt. Returns owned scalar evidence only.
  std::optional<RetirementReceipt> lastRetirement() const;
  // Called by a coalesced worker poll while idle; active waits pump internally.
  // Requests at most one stats response and does no file/profile writes.
  std::expected<void, AccountingError> poll();

  // Explicit owner-thread close before the GUI disappears. No new admissions.
  // Deadline bounds certification effort, not an OS/LT thread-join guarantee.
  // Failure returns uncertified; no destructor callback or inferred final0.
  CloseReceipt close(std::chrono::steady_clock::time_point deadline) noexcept;
private:
  friend class detail::PersistentCall;
  friend class detail::PersistentOperation;
  struct Impl;
  explicit PersistentSession(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
  friend std::expected<LocalReport, storage::Error> continueLocal(PersistentSession&,
      const storage::ProfileLease&, const LocalOptions&);
};

// New opt-in persistent path. Existing convenience API remains available for
// independent generated/CLI callers; its owner must still close/join explicitly.
// Success requires the SAME original payload/state completion prerequisites plus
// the matching storage-retirement fence. Stats failure alone is diagnosed, not
// translated into fake download failure or a fabricated exact final count.
std::expected<LocalReport, storage::Error> continueLocal(PersistentSession&,
    const storage::ProfileLease&, const LocalOptions&);

} // namespace biglybtc::engine
