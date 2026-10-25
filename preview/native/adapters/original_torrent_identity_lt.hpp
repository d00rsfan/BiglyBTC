#pragma once
#include "original_torrent_identity.hpp"
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
namespace biglybtc::storage {
// Qualified LT hashes are accepted only with its exact retained raw info span.
// Generic identity/EVP users do not depend on this optional LT-bound header.
inline std::expected<OriginalTorrentIdentity,Error> originalTorrentIdentityFromLibtorrent(
    const core::Document& document,const libtorrent::torrent_info& metadata) {
  const auto subject=originalTorrentInfoSubject(document);if(!subject){return std::unexpected(subject.error());}
  const auto section=metadata.info_section();
  if(section.size()!=static_cast<std::ptrdiff_t>(subject->size())||!std::equal(section.begin(),section.end(),subject->begin())) {
    return std::unexpected(Error{ErrorCode::Conflict,"Engine hashes are bound to different raw info bytes"});
  }
  OriginalFullInfoHashes full;const auto hashes=metadata.info_hashes();
  if(hashes.has_v1()){full.v1.emplace();std::copy_n(reinterpret_cast<const std::byte*>(hashes.v1.data()),20,full.v1->begin());}
  if(hashes.has_v2()){full.v2.emplace();std::copy_n(reinterpret_cast<const std::byte*>(hashes.v2.data()),32,full.v2->begin());}
  return selectOriginalTorrentIdentity(document,{*subject,std::move(full)});
}
} // namespace biglybtc::storage
