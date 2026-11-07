// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
namespace biglybtc::storage {class TorrentIntakeMetadata;}
namespace biglybtc::engine {
class PersistentSession;
// After an owned host starts, refresh only the existing admitted tracker of
// this actual fully verified seed. Success means queued, not delivered.
std::expected<void,storage::Error> announceSharedSeed(PersistentSession&,
    const storage::ProfileLease&,const storage::TorrentIntakeMetadata&,
    const std::filesystem::path& destination);
}
