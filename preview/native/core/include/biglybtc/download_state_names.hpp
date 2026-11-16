// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <optional>
#include <string_view>
namespace biglybtc::core::tags {
using DownloadStateNames=std::array<std::u16string_view,15>;
// Exact requested original locale spelling. Unknown locales and custom bundles
// have no stock-catalogue claim. Returned strings own static immutable storage.
std::optional<DownloadStateNames> stockDownloadStateNames(std::string_view locale)noexcept;
}
