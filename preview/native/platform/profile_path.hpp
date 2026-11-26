// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>

namespace biglybtc::platform {

std::filesystem::path defaultBiglyBtProfile(const std::filesystem::path& homeDirectory);
bool isGeneratedTransferProfile(const std::filesystem::path& profile);

} // namespace biglybtc::platform
