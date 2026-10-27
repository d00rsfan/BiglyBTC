#pragma once
#include "profile.hpp"
#include "biglybtc/torrent_layout.hpp"
namespace biglybtc::storage {
// Sole-lease read of fixed biglybt.config; no filesystem/config write or charset
// guessing. Missing field/file has original default. Recovery .saving refuses.
std::expected<std::string,Error> readOriginalFilenameConversions(const ProfileLease&);
std::expected<compat::OriginalLayout,Error> readOriginalLayout(const ProfileLease&,
    const core::Document&,std::size_t maximumFiles=100000);
}
