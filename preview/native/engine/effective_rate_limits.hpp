#pragma once

#include "biglybtc/effective_rate_limits.hpp"

#include <libtorrent/fwd.hpp>

namespace libtorrent { struct settings_pack; }
namespace biglybtc::engine::detail {
// Project only explicit keys of a pack obtained from the real get_settings().
// Partial packs do not imply default0. This does not apply or change settings.
core::SessionRateLimits projectEffectiveRateLimits(const libtorrent::settings_pack&) noexcept;
// Owner worker only; copied once after this private session is constructed.
// The same pack also supplies compact connection/slot/proxy diagnostics.
// No later settings mutation exists in this owner; a future mutation must
// explicitly refresh or invalidate the snapshot. Failure remains unavailable.
core::SessionRateLimits readEffectiveRateLimits(const libtorrent::session&) noexcept;
} // namespace biglybtc::engine::detail
