// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace biglybtc::core {
enum class OriginalTagOrderError { InvalidUtf8, Limit, ReferenceDifference, Memory };
inline constexpr std::size_t OriginalTagOrderMaximumUnits = 8192;

// Value only: valid bounded UTF-8 decoded without normalization, whose UTF-16
// units have equal measured Character properties in Java17 and Java26. This is
// neither a saved-source proof nor permission to edit/resolve/localize a tag.
class OriginalTagSortKey final {
public:
  OriginalTagSortKey(const OriginalTagSortKey&) = default;
  OriginalTagSortKey& operator=(const OriginalTagSortKey&) = default;
  OriginalTagSortKey(OriginalTagSortKey&&) noexcept = default;
  OriginalTagSortKey& operator=(OriginalTagSortKey&&) noexcept = default;
private:
  std::u16string units_;
  explicit OriginalTagSortKey(std::u16string value) : units_(std::move(value)) {}
  friend std::expected<OriginalTagSortKey, OriginalTagOrderError> originalTagSortKey(std::string_view);
  friend int compareOriginalTagSortKeys(const OriginalTagSortKey&, const OriginalTagSortKey&) noexcept;
};
std::expected<OriginalTagSortKey, OriginalTagOrderError> originalTagSortKey(std::string_view utf8);
// Exact FormattersImpl comparison integer, not Unicode folding or numeric parse.
int compareOriginalTagSortKeys(const OriginalTagSortKey&, const OriginalTagSortKey&) noexcept;
} // namespace biglybtc::core
