// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
namespace biglybtc::storage {
// Source/owner work policy,independent of SDK implementations and UI. The
// larger cohort is compiled only for the exact qualified dynamic-class role.
#if defined(BIGLYBTC_SCALABLE_TAG_RATE_RUNTIME) && BIGLYBTC_SCALABLE_TAG_RATE_RUNTIME
inline constexpr std::size_t MaximumTagRateGroups=256;
#else
inline constexpr std::size_t MaximumTagRateGroups=12;
#endif
}
