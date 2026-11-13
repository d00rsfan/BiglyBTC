// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "profile.hpp"
namespace biglybtc::storage {
std::expected<bool,Error> readRatingPersonalSort(const ProfileLease&);
std::expected<bool,Error> saveRatingPersonalSort(const ProfileLease&,bool personal);
}
