// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state_file_io.hpp"
#include "biglybtc/saved_search_engines.hpp"
namespace biglybtc::app::rss_detail {
struct Catalog {core::SavedSearchEngines value;bool primary=false;};
inline std::expected<Catalog,storage::Error> readCatalog(const storage::ProfileLease& lease){
  auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"RSS sources need an unnested profile read"});}
  const auto source=storage::ConfigFile::load(lease,"metasearch.config",{4*1024*1024,4*1024*1024,100000,64});
  if(!source){return std::unexpected(source.error());}
  auto rows=core::readSavedSearchEngines(source->document().originalBytes());
  if(!rows){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Saved RSS catalog is malformed or exceeds bounds"});}
  const auto saving=storage::detail::stateStampAt(lease.directoryHandle(),"metasearch.config.saving");
  const bool pending=!saving||saving->has_value();
  return Catalog{std::move(*rows),source->recoverySource()==storage::RecoverySource::Primary&&!pending};
}
} // namespace biglybtc::app::rss_detail
