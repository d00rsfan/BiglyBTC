// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "private_tag_rate_source.hpp"
#include <memory>
#include <libtorrent/info_hash.hpp>
namespace biglybtc::services::i2p::provider {
class ProfileSource;
// Metadata for the private friend-only batch,not a source/storage/SDK grant.
struct TagSourceRefresh {
 std::shared_ptr<ProfileSource> child;
 engine::PrivateTagRateSource source;
 libtorrent::info_hash_t expected;
};
}
