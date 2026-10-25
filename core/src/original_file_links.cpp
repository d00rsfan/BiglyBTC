#include "biglybtc/original_file_links.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <new>

namespace biglybtc::compat {
namespace {
bool utf8(std::string_view value) {
  for (std::size_t i = 0; i < value.size();) {
    const auto c = static_cast<unsigned char>(value[i++]);
    if (c < 0x80) { continue; }
    unsigned count = 0;
    std::uint32_t point = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; point = c & 0x1f; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; point = c & 0x0f; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; point = c & 7; minimum = 0x10000; }
    else { return false; }
    if (count > value.size() - i) { return false; }
    while (count-- != 0) {
      const auto next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xc0) != 0x80) { return false; }
      point = (point << 6) | (next & 0x3f);
    }
    if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) { return false; }
  }
  return true;
}

std::optional<std::int32_t> javaIndex(std::string_view value) {
  // String.trim() removes UTF-16 code units <= U+0020, including NUL.
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20) { value.remove_prefix(1); }
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20) { value.remove_suffix(1); }
  if (value.empty()) { return {}; }
  bool negative = false;
  if (value.front() == '-' || value.front() == '+') {
    negative = value.front() == '-'; value.remove_prefix(1);
  }
  if (value.empty()) { return {}; }
  const std::uint32_t bound = negative ? 2147483648U : 2147483647U;
  std::uint32_t number = 0;
  for (const unsigned char c : value) {
    // Original writers emit ASCII indices. Other numeral scripts do not gain
    // native path authority through locale/normalization guesses.
    if (c < '0' || c > '9') { return {}; }
    const auto digit = static_cast<std::uint32_t>(c - '0');
    if (number > (bound - digit) / 10) { return {}; }
    number = number * 10 + digit;
  }
  if (!negative || number == 0) { return static_cast<std::int32_t>(number); }
  if (number == 2147483648U) { return std::numeric_limits<std::int32_t>::min(); }
  return -static_cast<std::int32_t>(number);
}
} // namespace

std::expected<OriginalFileLinks, OriginalFileLinksError> resolveOriginalFileLinks(
    const core::Node* links, std::span<const std::string> bases) {
  using Code = OriginalFileLinksErrorCode;
  try {
    if (bases.size() > MaximumOriginalLinkFiles) {
      return std::unexpected(OriginalFileLinksError{Code::Limit, 0, "Original link file count exceeds its bound"});
    }
    if (links && links->type != core::Node::Type::List) {
      return std::unexpected(OriginalFileLinksError{Code::Type, 0, "Original filelinks2 is not a list"});
    }
    if (links && links->children.size() > MaximumOriginalLinkRows) {
      return std::unexpected(OriginalFileLinksError{Code::Limit, 0, "Original link row count exceeds its bound"});
    }
    // Duplicate bases cannot authorize a migrated file index. Keeping ambiguity
    // in the index also makes the common all-indexed case independent of it.
    struct Match { std::size_t index; bool ambiguous; };
    std::map<std::string_view, Match, std::less<>> byPath;
    std::size_t baseBytes = 0;
    for (std::size_t i = 0; i < bases.size(); ++i) {
      const auto& base = bases[i];
      if (base.empty() || base.front() != '/' || base.size() > 4096
          || base.find('\0') != base.npos || base.find('\n') != base.npos || !utf8(base)) {
        return std::unexpected(OriginalFileLinksError{Code::InvalidBase, i, "Invalid original absolute base path"});
      }
      if (base.size() > MaximumOriginalLinkBytes - baseBytes) {
        return std::unexpected(OriginalFileLinksError{Code::Limit, i, "Original base paths exceed their byte bound"});
      }
      baseBytes += base.size();
      auto [where, inserted] = byPath.emplace(base, Match{i, false});
      if (!inserted) { where->second.ambiguous = true; }
    }
    OriginalFileLinks result;
    result.byIndex.resize(bases.size());
    if (!links) { return result; }
    auto diagnostic = [&](OriginalFileLinkDiagnosticCode code, std::size_t row) {
      result.diagnostics.push_back({code, row});
    };
    std::size_t totalBytes = 0;
    for (std::size_t row = 0; row < links->children.size(); ++row) {
      const auto& item = links->children[row];
      if (item.type != core::Node::Type::Bytes) {
        diagnostic(OriginalFileLinkDiagnosticCode::NonStringRow, row); continue;
      }
      auto value = item.bytes;
      if (value.size() > MaximumOriginalLinkRowBytes || value.size() > MaximumOriginalLinkBytes - totalBytes) {
        return std::unexpected(OriginalFileLinksError{Code::Limit, row, "Original link strings exceed their byte bound"});
      }
      totalBytes += value.size();
      if (!utf8(value)) {
        // Java replaces malformed UTF-8 before lookup. Native does not guess a
        // possibly different filename; preserve the document and refuse use.
        return std::unexpected(OriginalFileLinksError{Code::Encoding, row, "Original link contains malformed UTF-8"});
      }
      // Java String.split("\\n") discards all trailing empty fields.
      while (!value.empty() && value.back() == '\n') { value.remove_suffix(1); }
      const auto first = value.find('\n');
      if (first == value.npos) { diagnostic(OriginalFileLinkDiagnosticCode::MalformedRow, row); continue; }
      const auto parsed = javaIndex(value.substr(0, first));
      if (!parsed) { diagnostic(OriginalFileLinkDiagnosticCode::InvalidIndex, row); continue; }
      const auto second = value.find('\n', first + 1);
      const auto from = value.substr(first + 1, second == value.npos ? value.npos : second - first - 1);
      std::size_t index = 0;
      const bool migrated = *parsed < 0;
      if (migrated) {
        const auto found = byPath.find(from);
        if (found == byPath.end()) { diagnostic(OriginalFileLinkDiagnosticCode::UnresolvedNegative, row); continue; }
        if (found->second.ambiguous) {
          return std::unexpected(OriginalFileLinksError{Code::AmbiguousBase, row, "Negative original link matches multiple file indices"});
        }
        index = found->second.index;
      } else {
        index = static_cast<std::size_t>(*parsed);
        if (index >= bases.size()) { diagnostic(OriginalFileLinkDiagnosticCode::IndexedOutsideMetadata, row); continue; }
      }
      if (second == value.npos) { result.byIndex[index].reset(); continue; }
      const auto third = value.find('\n', second + 1);
      const auto target = value.substr(second + 1, third == value.npos ? value.npos : third - second - 1);
      if (third != value.npos) { diagnostic(OriginalFileLinkDiagnosticCode::ExtraFieldsIgnored, row); }
      if (!from.empty() && target == from) { result.byIndex[index].reset(); continue; }
      result.byIndex[index] = OriginalFileLink{std::string(from), std::string(target), row, migrated};
    }
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(OriginalFileLinksError{Code::Memory, 0, "Insufficient memory for original file links"});
  }
}
} // namespace biglybtc::compat
