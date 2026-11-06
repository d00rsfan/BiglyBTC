// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/original_tag_order.hpp"
#include "original_tag_order_properties.hpp"

#include <algorithm>
#include <new>

namespace biglybtc::core {
static_assert(OriginalTagOrderMaximumUnits == tag_order::MaximumTextUnits);
std::expected<OriginalTagSortKey, OriginalTagOrderError> originalTagSortKey(std::string_view utf8) {
  if (utf8.size() > OriginalTagOrderMaximumUnits * 4) {
    return std::unexpected(OriginalTagOrderError::Limit);
  }
  try {
    std::u16string units;
    units.reserve(std::min(utf8.size(), OriginalTagOrderMaximumUnits));
    for (std::size_t at = 0; at < utf8.size();) {
      const auto first = static_cast<unsigned char>(utf8[at++]);
      std::uint32_t point = first;
      unsigned extra = 0;
      if (first <= 0x7f) {}
      else if (first >= 0xc2 && first <= 0xdf) { point = first & 0x1fU; extra = 1; }
      else if (first >= 0xe0 && first <= 0xef) { point = first & 0x0fU; extra = 2; }
      else if (first >= 0xf0 && first <= 0xf4) { point = first & 0x07U; extra = 3; }
      else { return std::unexpected(OriginalTagOrderError::InvalidUtf8); }
      if (extra > utf8.size() - at) { return std::unexpected(OriginalTagOrderError::InvalidUtf8); }
      for (unsigned n = 0; n < extra; ++n) {
        const auto byte = static_cast<unsigned char>(utf8[at++]);
        if ((byte & 0xc0U) != 0x80U) { return std::unexpected(OriginalTagOrderError::InvalidUtf8); }
        point = (point << 6) | (byte & 0x3fU);
      }
      if ((extra == 1 && point < 0x80) || (extra == 2 && point < 0x800)
          || (extra == 3 && point < 0x10000) || point > 0x10ffff
          || (point >= 0xd800 && point <= 0xdfff)) {
        return std::unexpected(OriginalTagOrderError::InvalidUtf8);
      }
      const auto needed = point <= 0xffff ? 1U : 2U;
      if (needed > OriginalTagOrderMaximumUnits - units.size()) {
        return std::unexpected(OriginalTagOrderError::Limit);
      }
      const char16_t firstUnit = static_cast<char16_t>(point <= 0xffff
          ? point : 0xd800U + ((point - 0x10000U) >> 10));
      const auto properties = tag_order::measured::CommonProperties{};
      if (!properties(firstUnit)) { return std::unexpected(OriginalTagOrderError::ReferenceDifference); }
      units.push_back(firstUnit);
      if (needed == 2) {
        const auto secondUnit = static_cast<char16_t>(0xdc00U + ((point - 0x10000U) & 0x3ffU));
        if (!properties(secondUnit)) { return std::unexpected(OriginalTagOrderError::ReferenceDifference); }
        units.push_back(secondUnit);
      }
    }
    return OriginalTagSortKey(std::move(units));
  } catch (const std::bad_alloc&) {
    return std::unexpected(OriginalTagOrderError::Memory);
  }
}
int compareOriginalTagSortKeys(const OriginalTagSortKey& a, const OriginalTagSortKey& b) noexcept {
  // Private construction and immutable measured tables ensure both values are
  // bounded/common. No runtime source, locale or mutable policy is consulted.
  return *tag_order::compareNatural(a.units_, b.units_, tag_order::measured::CommonProperties{});
}
} // namespace biglybtc::core
