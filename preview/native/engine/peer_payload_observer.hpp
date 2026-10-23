#pragma once

#include "biglybtc/peer_payload_ledger.hpp"
#include <libtorrent/fwd.hpp>

#include <cstdint>
#include <memory>


namespace biglybtc::engine::detail {
// Numeric equivalent of pinned LT2.0.12 is_local(address)||is_loopback().
// Unknown family remains Unknown; this is not a geographic/public-IP test.
core::PeerNetworkClass classifyPeerPayloadEndpoint(const core::PeerNumericEndpoint&) noexcept;
// Called by the worker BEFORE add_torrent. Only the returned extension's LT
// callbacks touch a peer handle. No callback points into the Host or owner.
void attachPeerPayloadObserver(libtorrent::add_torrent_params&,
    const std::shared_ptr<core::PeerPayloadLedger>&, std::uint64_t operation) noexcept;
} // namespace biglybtc::engine::detail
