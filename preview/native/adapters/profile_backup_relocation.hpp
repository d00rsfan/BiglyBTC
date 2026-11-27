// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"

namespace biglybtc::storage::detail {

std::expected<std::string, Error> relocateBackupConfiguration(std::string original,
    std::string_view previousDirectory, std::string_view newDirectory);

} // namespace biglybtc::storage::detail
