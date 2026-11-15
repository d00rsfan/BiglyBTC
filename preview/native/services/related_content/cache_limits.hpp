// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
namespace biglybtc::services::related_content {
// Resource budgets for original saved caches, independent of the configured
// result limit. Public network association/search packet limits are unchanged.
inline constexpr std::size_t MaximumCacheBytes=1024U*1024U*1024U;
inline constexpr std::size_t MaximumItems=2000000;
inline constexpr std::size_t MaximumCacheNodes=128000000;
inline constexpr core::Limits cacheFileLimits(){return {MaximumCacheBytes,MaximumCacheBytes,2000000,128};}
}
