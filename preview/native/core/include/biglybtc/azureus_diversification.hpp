// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace biglybtc::core {
// Original diverse.dat remote entry. Metadata only; no contact/IO authority.
struct AzureusRemoteDiversification {std::array<std::uint8_t,20> key{};std::int8_t type=1;std::int64_t expiry=0;std::array<unsigned,2> offsets{};bool operator==(const AzureusRemoteDiversification&)const=default;};
}
