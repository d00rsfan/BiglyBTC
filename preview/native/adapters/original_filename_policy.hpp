#pragma once
#include "profile.hpp"
#include "biglybtc/torrent_layout.hpp"
namespace biglybtc::storage {
// Sole-lease read of fixed biglybt.config; no filesystem/config write or charset
// guessing. Missing field/file has original default. Recovery .saving refuses.
struct OriginalFilenameContext {
  std::string conversions;
  bool decoderShowAll=false;
  bool operator==(const OriginalFilenameContext&) const = default;
};
// Both settings are projected from ONE owned config read. This is an explicit
// registry capability only; no default decoder/chooser/Fallback is inferred.
std::expected<OriginalFilenameContext,Error> readOriginalFilenameContext(const ProfileLease&);
std::expected<bool,Error> readOriginalDecoderShowAll(const ProfileLease&);
std::expected<std::string,Error> readOriginalFilenameConversions(const ProfileLease&);
std::expected<compat::OriginalLayout,Error> readOriginalLayout(const ProfileLease&,
    const core::Document&,std::size_t maximumFiles=100000);
}
