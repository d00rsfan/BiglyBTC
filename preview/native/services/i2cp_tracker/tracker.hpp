// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "messages.hpp"
#include "biglybtc/udp_tracker_wire.hpp"
namespace biglybtc::services::i2p::i2cp {
inline constexpr Byte HostLookupType=38,HostReplyType=39,RawTrackerProtocol=18;
inline constexpr std::uint16_t TrackerLocalPort=9000;
inline constexpr std::size_t MaximumTrackerOperations=1024; // Lifetime nonreuse of wire transaction IDs.
struct HostLookup {std::uint16_t sessionId=0;std::uint32_t request=0,timeoutMilliseconds=0;Hash hash{};};
struct HostReply {std::uint16_t sessionId=0;std::uint32_t request=0;Byte result=0;std::optional<Destination> destination;};
// Exact original LOOKUP_HASH/default reply only. No hostname/OS DNS/options.
[[nodiscard]] ConfigResult<Bytes> encodeHostLookup(const HostLookup&);
[[nodiscard]] ConfigResult<HostLookup> decodeHostLookup(const Frame&);
[[nodiscard]] ConfigResult<Bytes> encodeHostReply(const HostReply&);
[[nodiscard]] ConfigResult<HostReply> decodeHostReply(const Frame&);
struct TrackerRequest {Hash destination{};std::array<Byte,20> infoHash{};std::uint16_t port=0;int deadlineMilliseconds=30000;};
enum class TrackerPhase {LookingUp,Connecting,Scraping,Counts,LookupFailed,TrackerError,UnknownReply,DestinationMismatch,LocalCancelled,LocalTimeout,LocalLeaseExpired};
struct TrackerCounts {std::int32_t seeds=0,downloaded=0,peers=0;};
struct TrackerObservation {std::uint64_t operation=0;TrackerRequest request;TrackerPhase phase=TrackerPhase::LookingUp;std::optional<TrackerCounts> counts;Byte lookupResult=0;std::uint64_t rejectedReplies=0;};
// RAW18 has no authenticated sender field. Counts remain confined to this
// explicit operation; never payload, peer discovery, tracker/rank authority.
} // namespace biglybtc::services::i2p::i2cp
