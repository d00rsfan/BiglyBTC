// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include <string_view>
namespace biglybtc::core::tags {
// Exact known stock tag.* key and explicitly observed original locale only.
// Custom/plugin bundles and absent keys remain unavailable. Static storage.
std::optional<std::u16string_view> stockTagName(std::string_view locale,std::u16string_view key)noexcept;
}
