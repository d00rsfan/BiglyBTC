#pragma once
#include "biglybtc/peer_payload_ledger.hpp"

#include <array>
#include <expected>
#include <span>

namespace biglybtc::core {
enum class CountryLookupKind { Unavailable, Unknown, Country };
struct CountryLookup {
  CountryLookupKind kind = CountryLookupKind::Unavailable;
  std::array<char,3> code{};
  std::array<char,253> name{}; // Bounded English DB name, not interpreted markup.
};
struct CountryResolvedPeer {
  std::uint64_t connection = 0;
  CountryLookup location{};
};
enum class CountryRowKind { Local, Unavailable, Unknown, Country };
inline constexpr std::size_t MaximumCountryRows = MaximumPeerPayloadRecords + 3;
struct CountryRow {
  CountryRowKind kind = CountryRowKind::Unavailable;
  std::array<char,3> code{};
  std::array<char,253> name{};
  // Recorded includes retained retired records; rate counts cover active only.
  std::size_t recordedPeers = 0, rateObservedPeers = 0, rateMissingPeers = 0;
  // Sum ONLY the available last-observed peer payload low-pass rates. This is
  // not an exhaustive current country rate, lifetime total, or wire traffic.
  std::optional<std::int64_t> observedDownloadRate{}, observedUploadRate{};
  // Observed byte prefixes exclude restored raw totals and lost intervals.
  std::optional<std::int64_t> observedDownloadBytes{}, observedUploadBytes{};
  std::size_t downloadBytePeers = 0, uploadBytePeers = 0, retiredPeers = 0;
  bool downloadBytesOverflow = false, uploadBytesOverflow = false;
  bool partial = true;
};
struct CountryStatistics {
  PeerPayloadIdentity identity{};
  std::uint64_t sequence = 0; // Ledger revision; zero allowed before any peer.
  bool providerAvailable = false;
  std::uint32_t ledgerIssues = 0;
  std::size_t retainedConnections = 0, includedConnections = 0, retiredConnections = 0;
  std::size_t rowCount = 0;
  std::array<CountryRow,MaximumCountryRows> rows{};
  bool partial = true, tailUncertain = true;
};
enum class CountryStatisticsError { InvalidLedger, InvalidResolution };
// Fixed, owned scalar projection. Resolutions are matched by connection token;
// Local records bypass geographic classification. Retired byte prefixes remain
// grouped but never contribute current rates. Missing resolutions stay
// Unavailable; a DB-confirmed no-entry result is Unknown. Retired rows are not
// treated as current rates. Raw LT carried-over totals are NEVER consumed.
std::expected<CountryStatistics,CountryStatisticsError> projectCountryStatistics(
    const PeerPayloadSnapshot&, std::span<const CountryResolvedPeer>, bool providerAvailable) noexcept;
bool validCountryStatistics(const CountryStatistics&) noexcept;
} // namespace biglybtc::core
