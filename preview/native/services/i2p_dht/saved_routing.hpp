// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_contacts.hpp"
#include "../azureus_router/router.hpp"
namespace biglybtc::services::i2p::dht {
// Value-only original DHTControlImpl.exportState selection. Imported rows are
// untrusted hints; their saved timeAlive never restores router liveness/time.
// The actor must obtain bestContacts under its actual held source grant.
// Passive version metadata from accepted current requests/matched replies.
// It never makes an ID imported, alive, authenticated or eligible for effects.
struct RoutingProtocolObservation {Id id{};Hash hash{};std::uint16_t port=0;std::int8_t protocolVersion=0;};
Result<std::vector<SavedContact>> selectSavedRoutingContacts(
    const azureus_router::RouterBestContacts&,std::span<const Contact> attachments,
    std::span<const SavedContact> imported,
    std::span<const RoutingProtocolObservation> observed={});
}
