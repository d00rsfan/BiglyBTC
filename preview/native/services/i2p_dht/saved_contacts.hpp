// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dht.hpp"
#include <cstdint>
#include <span>

namespace biglybtc::services::i2p::dht {
// Original DHTPluginStorageManager calls exportState(...,32). Parsing memory
// does not import a router contact or restore Alive/source/network authority.
inline constexpr std::size_t MaximumSavedContacts=32,MaximumSavedDestinationBytes=512;
inline constexpr std::size_t MaximumSavedContactsBytes=4+MaximumSavedContacts*(8+1+1+20+4+1+32+2+MaximumSavedDestinationBytes+4);
struct SavedContact {
  Contact contact;
  std::int64_t timeAlive=0; // Original signed long, an untrusted historical hint.
  std::uint8_t serializationVersion=1;
  std::int8_t protocolVersion=0; // Original readInt -> (byte), writeInt(byte).
};
Result<std::vector<SavedContact>> decodeSavedContacts(View);
// Caller supplies the already-selected original-priority order, at most32.
// No sorting, deduplication, alive classification or file publication occurs.
Result<Bytes> encodeSavedContacts(std::span<const SavedContact>);
enum class SavedContactsSource {Primary,Saving};
struct SavedContactsFile {SavedContactsSource source;std::vector<SavedContact> contacts;};
// Presence is distinct from empty/malformed bytes. Original fallback chooses
// contacts.saving ONLY when contacts.dat is absent, never when dat is corrupt.
Result<std::optional<SavedContactsFile>> selectSavedContactsFile(
    std::optional<View> primary,std::optional<View> saving);
} // namespace biglybtc::services::i2p::dht
