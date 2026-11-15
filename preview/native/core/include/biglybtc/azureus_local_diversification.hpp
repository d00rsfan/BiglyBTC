// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace biglybtc::core {
// Original diverse.dat local storageKey checkpoint, not a stored DHT value.
// Type1 updates remove the known local checkpoint; it is not serialised.
struct AzureusLocalDiversification {
 std::array<std::uint8_t,20> key{};std::int8_t type=1;std::int64_t expiry=0;
 bool operator==(const AzureusLocalDiversification&)const=default;
};
}
