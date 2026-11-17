// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "original_torrent_identity.hpp"
namespace biglybtc::storage {
// Exact pristine raw info bytes only. No file access,geometry,payload,selected
// swarm capability or writer permission is inferred from a computed identity.
std::expected<OriginalTorrentIdentity,Error> digestOriginalTorrentIdentity(const core::Document&);
}
