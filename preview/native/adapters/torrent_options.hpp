// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <array>
namespace biglybtc::storage {
struct OriginalTorrentOptions {std::array<int,4> values{};bool stopped=false;};
std::expected<OriginalTorrentOptions,Error> readOriginalTorrentOptions(const ProfileLease&,const core::TorrentHash&);
// Caller must first join/forget every owner of this target. Each operation
// edits one original file only; resume/info and unrelated fields stay intact.
std::expected<void,Error> saveOriginalTorrentOptions(const ProfileLease&,const core::TorrentHash&,bool rates,
    std::array<int,2> expected,std::array<int,2> desired);
}
