// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include <stop_token>

namespace biglybtc::storage::detail {

std::expected<std::string, Error> relocateBackupConfiguration(std::string original,
    std::string_view previousDirectory, std::string_view newDirectory);

std::expected<std::string, Error> pauseRestoredProfileDownloads(std::string original, std::stop_token);

} // namespace biglybtc::storage::detail
