#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace biglybtc::core {

inline constexpr std::size_t MaximumPeerPayloadRecords = 512;
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

struct PeerPayloadIdentity {
  std::uint64_t applicationRun = 0, profileSession = 0, engineEpoch = 0;
  bool operator==(const PeerPayloadIdentity&) const = default;
};
enum class PeerAddressFamily : std::uint8_t { Unknown, V4, V6 };
struct PeerNumericEndpoint {
  PeerAddressFamily family = PeerAddressFamily::Unknown;
  // Network-order bytes. V4 uses only the first four; the remainder is zero.
  std::array<std::uint8_t, 16> address{};
  std::uint16_t port = 0;
  std::uint64_t scope = 0;
  bool operator==(const PeerNumericEndpoint&) const = default;
};
// LT's local-network predicate, not a location/country classification.
enum class PeerNetworkClass : std::uint8_t { Unknown, Local, NonLocal };
enum class PeerPayloadPhase : std::uint8_t { Attached, Connected, Disconnected, Released };
enum class PeerPayloadIssue : std::uint32_t {
  None = 0, InvalidIdentity = 1U << 0, Capacity = 1U << 1,
  Contention = 1U << 2, Synchronization = 1U << 3, Factory = 1U << 4,
  UnsupportedConnection = 1U << 5, EndpointUnavailable = 1U << 6,
  SampleUnavailable = 1U << 7, InvalidSample = 1U << 8,
  CounterRegression = 1U << 9, Clock = 1U << 10,
  SequenceExhausted = 1U << 11, InvalidToken = 1U << 12,
  MissingDisconnect = 1U << 13, Lifecycle = 1U << 14, UnsupportedAbi = 1U << 15,
  ReceivedPrefixOverflow = 1U << 16, SentPrefixOverflow = 1U << 17
};
constexpr bool hasPeerPayloadIssue(std::uint32_t mask, PeerPayloadIssue issue) noexcept {
  return (mask & static_cast<std::uint32_t>(issue)) != 0;
}
struct PeerPayloadSample {
  // Raw LT peer statistics, including any restored prior-connection amounts.
  // These are NOT newly transferred connection bytes and must not be summed.
  std::int64_t received = 0, sent = 0;
  // LT's payload low-pass rates, bytes/sec. No protocol, IP, tracker or DHT.
  std::int32_t receivedRate = 0, sentRate = 0;
  bool operator==(const PeerPayloadSample&) const = default;
};
struct PeerPayloadRecord {
  std::uint64_t connection = 0, operation = 0, sequence = 0;
  PeerNumericEndpoint remote{};
  PeerNetworkClass network = PeerNetworkClass::Unknown;
  PeerPayloadPhase phase = PeerPayloadPhase::Attached;
  std::int64_t attachedNanoseconds = 0, eventNanoseconds = 0;
  std::optional<PeerPayloadSample> lastObserved{};
  std::optional<std::int64_t> observedNanoseconds{};
  // A rejected latest read does not erase the preceding owned observation.
  bool latestSampleAvailable = false;
  // First valid raw observation establishes a baseline and contributes zero.
  // Only consecutive valid monotone differences contribute; gaps rebaseline.
  std::optional<std::int64_t> observedReceivedBytes{}, observedSentBytes{};
  bool byteBaselineAvailable = false;
  bool disconnectObserved = false;
  // The LT disconnect callback is not a complete payload/retirement fence.
  bool tailUncertain = true;
  std::uint32_t issues = 0;
};
struct PeerPayloadSnapshot {
  PeerPayloadIdentity identity{};
  std::uint64_t sequence = 0;
  std::uint32_t issues = 0;
  std::size_t count = 0;
  std::array<PeerPayloadRecord, MaximumPeerPayloadRecords> records{};
  // Latest records, not a lossless event log. No complete/global total exists.
};

static_assert(sizeof(PeerPayloadSnapshot) <= 128 * 1024);

// Fixed storage, no heap allocation in mutations. The callback thread and the
// worker use try_lock only. Contention makes availability explicitly lossy;
// essential transfer/accounting code never waits for this optional observer.
class PeerPayloadLedger final {
public:
  explicit PeerPayloadLedger(PeerPayloadIdentity) noexcept;
  std::uint64_t attach(std::uint64_t operation, const PeerNumericEndpoint&,
      PeerNetworkClass, std::int64_t nowNanoseconds) noexcept;
  void connected(std::uint64_t, std::int64_t nowNanoseconds) noexcept;
  void observe(std::uint64_t, std::int64_t nowNanoseconds,
      const std::optional<PeerPayloadSample>&, bool disconnect = false) noexcept;
  // Destructor path: scalar metadata only, never a read of an expired peer.
  void released(std::uint64_t) noexcept;
  void noteLoss(PeerPayloadIssue) noexcept;
  std::optional<PeerPayloadSnapshot> snapshot() const noexcept;
private:
  bool advance(PeerPayloadRecord* = nullptr) noexcept;
  PeerPayloadRecord* find(std::uint64_t) noexcept;
  bool eventTime(PeerPayloadRecord&, std::int64_t) noexcept;
  void issue(PeerPayloadRecord&, PeerPayloadIssue) noexcept;
  mutable std::mutex mutex_;
  mutable std::atomic<std::uint32_t> issues_{0};
  mutable std::atomic_bool rebaseline_{false};
  PeerPayloadSnapshot state_{};
};

} // namespace biglybtc::core
