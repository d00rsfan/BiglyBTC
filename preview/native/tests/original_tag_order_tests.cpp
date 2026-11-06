// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/original_tag_order.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace core = biglybtc::core;
namespace {
void check(bool value, const char* why) { if (!value) { throw std::runtime_error(why); } }
#include "original_tag_order_corpus.inc"
std::string utf8(std::uint32_t point) {
  check(point <= 0xffff, "BMP test point");
  if (point < 0x80) { return std::string(1, static_cast<char>(point)); }
  if (point < 0x800) {
    return {static_cast<char>(0xc0U | (point >> 6)), static_cast<char>(0x80U | (point & 63U))};
  }
  return {static_cast<char>(0xe0U | (point >> 12)), static_cast<char>(0x80U | ((point >> 6) & 63U)),
          static_cast<char>(0x80U | (point & 63U))};
}
void genuineOriginalCorpus() {
  static_assert(!std::is_default_constructible_v<core::OriginalTagSortKey>);
  for (const auto& row : Corpus) {
    auto a = core::originalTagSortKey(row.first), b = core::originalTagSortKey(row.second);
    check(a.has_value() && b.has_value(), "actual shared-reference literal key refused");
    check(core::compareOriginalTagSortKeys(*a, *b) == row.expected,
          "comparison differs from retained actual Java17/Java26 result");
  }
}
void utf8BoundsAndExplicitReferenceDifference() {
  for (const auto& bytes : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xc1\xbf"),
      std::string("\xe0\x80\xaf"), std::string("\xed\xa0\x80"), std::string("\xf0\x80\x80\xaf"),
      std::string("\xf4\x90\x80\x80"), std::string("\xf5\x80\x80\x80"), std::string("\xe2\x82"),
      std::string("\xe2\x28\xa1")}) {
    const auto key = core::originalTagSortKey(bytes);
    check(!key && key.error() == core::OriginalTagOrderError::InvalidUtf8,
          "malformed/overlong/surrogate/non-scalar UTF8 silently replaced");
  }
  constexpr std::array<std::uint32_t, 13> different{
      0x1c89, 0x2c2f, 0xa7c0, 0xa7cb, 0xa7cc, 0xa7ce, 0xa7d0,
      0xa7d2, 0xa7d4, 0xa7d6, 0xa7d8, 0xa7da, 0xa7dc};
  for (const auto point : different) {
    const auto key = core::originalTagSortKey(utf8(point));
    check(!key && key.error() == core::OriginalTagOrderError::ReferenceDifference,
          "measured JDK-dependent Character mapping silently selected");
  }
  auto maximum = core::originalTagSortKey(std::string(core::OriginalTagOrderMaximumUnits, 'a'));
  check(maximum.has_value(), "maximum bounded key refused");
  auto tooMany = core::originalTagSortKey(std::string(core::OriginalTagOrderMaximumUnits + 1, 'a'));
  check(!tooMany && tooMany.error() == core::OriginalTagOrderError::Limit, "UTF16 unit bound ignored");
  auto tooLarge = core::originalTagSortKey(std::string(core::OriginalTagOrderMaximumUnits * 4 + 1, 'a'));
  check(!tooLarge && tooLarge.error() == core::OriginalTagOrderError::Limit, "input-byte bound ignored");
  const auto nul = core::originalTagSortKey(std::string("a\0b", 3));
  check(nul && core::compareOriginalTagSortKeys(*nul, *nul) == 0, "valid UTF8 unit was normalized away");
}
} // namespace
int main() {
  try {
    genuineOriginalCorpus(); utf8BoundsAndExplicitReferenceDifference();
    std::cout << "PASS original common-JDK Unicode tag value order\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
