// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"
#include <vector>

namespace biglybtc::storage::detail {

// Read names from an independent cursor on the held directory.
std::expected<std::vector<std::filesystem::path>, Error> listStateDirectoryNames(StateHandle, std::size_t maximum);

}
