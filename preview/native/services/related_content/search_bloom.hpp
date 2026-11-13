// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "search.hpp"
#include "bloom.hpp"
namespace biglybtc::services::related_content {
using SearchBloom=msgsync::AddOnlyBloom;
struct SearchBlooms {SearchBloom all,withoutDht;};
// Words match Java UTF-16 iteration, including surrogate-prefix replacement.
Result<std::vector<std::string>> searchWords(const Item&);
// Stable shuffle seed is injected entropy, never a permission to publish items.
Result<SearchBlooms> buildSearchBlooms(std::span<const Item> cached,
    std::span<const Item> dht,std::uint64_t shuffleSeed);
Result<bool> bloomMayMatch(const SearchBloom&,const SearchQuery&);
}
