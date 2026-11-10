// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <array>
namespace biglybtc::storage {
using ChatIdentityStamp=std::array<std::uint8_t,32>;
std::expected<ChatIdentityStamp,Error> chatIdentityStamp(const core::Document&,std::string_view key);
}
