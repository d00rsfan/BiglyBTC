// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace biglybtc::services::azureus_db_runtime::survey {
inline constexpr std::array<std::uint8_t,20> obfuscatedTarget(const std::array<std::uint8_t,20>& plain)noexcept{
 std::array<std::uint8_t,20> out{};for(std::size_t i=0;i<5;++i){out[i]=plain[i];}for(std::size_t i=6;i<20;++i){if(plain[i]==0){out[i]=1;}}out[18]=plain[18];out[19]=plain[19];return out;
}
}
