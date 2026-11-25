// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <span>

namespace biglybtc::storage {

// Observe indexed paths and the first missing component without creating any files.
std::expected<std::string, Error> captureIntakeDestinationSnapshot(const core::Document&,
    const compat::OriginalLayout&, const core::TorrentHash&, std::span<const std::filesystem::path>,
    std::span<const std::uint64_t> maxima = {});

} // namespace biglybtc::storage
