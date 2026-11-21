// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
// A plausible signature from the wrong image must never be called/admitted.
extern "C" std::uint64_t biglybtc_shared_quota_features_v1()noexcept{return UINT64_C(0x4242515300010001);}
