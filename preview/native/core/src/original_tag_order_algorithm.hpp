#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace biglybtc::core::tag_order {

inline constexpr std::size_t MaximumTextUnits = 8192;

struct CharacterProperty {
  char16_t lowercase;
  bool digit;
};

enum class Unavailable { TextLimit, ReferencePropertyDifference };

// Properties is a pure, nonthrowing, immutable BMP-table lookup. nullopt means
// the two measured reference JDKs disagree. This is text data, never authority.
template<class Properties>
std::expected<int, Unavailable> compareNatural(
    std::u16string_view a, std::u16string_view b, const Properties& properties) {
  if (a.size() > MaximumTextUnits || b.size() > MaximumTextUnits) {
    return std::unexpected(Unavailable::TextLimit);
  }
  // Check even units after an early ordering difference: a successful result
  // promises that both complete texts belong to the common reference domain.
  for (const auto text : {a, b}) {
    for (const auto unit : text) {
      if (!properties(unit)) {
        return std::unexpected(Unavailable::ReferencePropertyDifference);
      }
    }
  }
  std::size_t x = 0, y = 0;
  while (x < a.size() && y < b.size()) {
    const auto first = a[x++], second = b[y++];
    const auto p = *properties(first), q = *properties(second);
    if (p.digit && q.digit) {
      const auto ax = x - 1, by = y - 1;
      while (x < a.size() && properties(a[x])->digit) { ++x; }
      while (y < b.size() && properties(b[y])->digit) { ++y; }
      const auto an = x - ax, bn = y - by;
      if (an != bn) {
        return static_cast<int>(an) - static_cast<int>(bn);
      }
      for (std::size_t n = 0; n < an; ++n) {
        if (a[ax + n] != b[by + n]) {
          return static_cast<int>(a[ax + n]) - static_cast<int>(b[by + n]);
        }
      }
    } else if (p.lowercase != q.lowercase) {
      return static_cast<int>(p.lowercase) - static_cast<int>(q.lowercase);
    }
  }
  return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

// Manual-tag branch only. null group sorts after every non-null group, even "".
template<class Properties>
std::expected<int, Unavailable> compareManualTag(
    std::optional<std::u16string_view> aGroup, std::u16string_view aName,
    std::optional<std::u16string_view> bGroup, std::u16string_view bName,
    const Properties& properties) {
  // Validate every supplied value before reporting a key as supported, including
  // a name/group that an earlier group comparison would otherwise bypass.
  for (const auto value : {aName, bName}) {
    if (auto checked = compareNatural(value, {}, properties); !checked) {
      return std::unexpected(checked.error());
    }
  }
  for (const auto group : {aGroup, bGroup}) {
    if (group) {
      if (auto checked = compareNatural(*group, {}, properties); !checked) {
        return std::unexpected(checked.error());
      }
    }
  }
  if (aGroup.has_value() != bGroup.has_value()) { return aGroup ? -1 : 1; }
  if (aGroup) {
    auto compared = compareNatural(*aGroup, *bGroup, properties);
    if (!compared || *compared != 0) { return compared; }
  }
  return compareNatural(aName, bName, properties);
}

} // namespace biglybtc::core::tag_order
