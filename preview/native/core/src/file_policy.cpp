#include "biglybtc/file_policy.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <optional>

namespace biglybtc::compat {
namespace {
using core::Node;
struct Failure { std::string message; };
void require(bool condition, std::string message) { if (!condition) { throw Failure{std::move(message)}; } }
template<typename T> T number(std::string_view text) {
  // Java's parseLong/parseInt accept a leading plus and decimal leading zeros.
  if (text.starts_with('+')) {
    text.remove_prefix(1);
    require(!text.empty() && text.front() >= '0' && text.front() <= '9', "Invalid original signed file-priority number");
  }
  require(!text.empty(), "Empty original file-priority number");
  T value{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "Invalid original file-priority number");
  return value;
}
FilePolicy decode(std::int64_t saved) {
  // Long.intValue in the original intentionally retains the low 32 bits.
  const auto value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(saved));
  return {value == 0, value < 0 ? value + 1 : value};
}
}

std::expected<std::vector<FilePolicy>, PolicyError> originalFilePolicy(
    const Node& entry, std::size_t files, std::size_t maximumFiles) {
  try {
    require(entry.type == Node::Type::Dictionary && files <= maximumFiles && maximumFiles <= 100000,
        "Original file-priority count exceeds its bound");
    std::vector<FilePolicy> result(files);
    if (const auto* compressed = entry.find("file_priorities_c")) {
      require(compressed->type == Node::Type::Dictionary, "Invalid compressed original file priorities");
      std::vector<std::optional<FilePolicy>> assigned(files);
      std::size_t work = 0;
      for (std::size_t i = 0; i < compressed->children.size(); i += 2) {
        const auto& key = compressed->children[i];
        const auto& ranges = compressed->children[i + 1];
        require(key.type == Node::Type::Bytes && key.bytes.size() <= 24 && ranges.type == Node::Type::Bytes,
            "Invalid original file-priority group");
        const auto policy = decode(number<std::int64_t>(key.bytes));
        auto text = ranges.bytes;
        // String.split drops trailing empty comma fields in original Java.
        while (text.ends_with(',')) { text.remove_suffix(1); }
        require(!text.empty(), "Empty original file-priority ranges");
        while (!text.empty()) {
          const auto comma = text.find(',');
          const auto range = text.substr(0, comma);
          const auto dash = range.find('-');
          const auto first = number<std::int32_t>(range.substr(0, dash));
          const auto last = dash == std::string_view::npos ? first : number<std::int32_t>(range.substr(dash + 1));
          require(first >= 0 && last >= first && static_cast<std::uint64_t>(last) < files,
              "Original file-priority range is outside metadata");
          const auto count = static_cast<std::size_t>(last - first) + 1;
          require(work <= maximumFiles * 4 && count <= maximumFiles * 4 - work,
              "Original file-priority range work exceeds its bound");
          work += count;
          for (auto index = static_cast<std::size_t>(first); index <= static_cast<std::size_t>(last); ++index) {
            require(!assigned[index] || *assigned[index] == policy, "Conflicting original file-priority ranges");
            assigned[index] = policy;
            result[index] = policy;
          }
          if (comma == std::string_view::npos) { break; }
          text.remove_prefix(comma + 1);
        }
      }
    } else if (const auto* legacy = entry.find("file_priorities")) {
      require(legacy->type == Node::Type::List && legacy->children.size() <= maximumFiles,
          "Invalid legacy original file priorities");
      for (std::size_t i = 0; i < std::min(files, legacy->children.size()); ++i) {
        require(legacy->children[i].type == Node::Type::Integer, "Invalid legacy file-priority value");
        result[i] = decode(legacy->children[i].integer);
      }
    }
    return result;
  } catch (const Failure& failure) { return std::unexpected(PolicyError{failure.message}); }
}

std::expected<std::vector<std::uint8_t>, PolicyError> linearPriorityRanks(std::span<const FilePolicy> policy) {
  if (policy.size() > 100000) { return std::unexpected(PolicyError{"File-priority count exceeds its bound"}); }
  std::vector<std::int32_t> levels;
  for (const auto& file : policy) {
    if (file.skipped) { return std::unexpected(PolicyError{"Skipped files require their qualified storage adapter"}); }
    levels.push_back(file.priority);
  }
  std::ranges::sort(levels);
  levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
  if (levels.size() > 7) { return std::unexpected(PolicyError{"More than seven original priority ranks require their scheduling adapter"}); }
  std::vector<std::uint8_t> result;
  result.reserve(policy.size());
  for (const auto& file : policy) {
    result.push_back(levels.size() == 1 ? std::uint8_t{4}
        : static_cast<std::uint8_t>(1 + std::lower_bound(levels.begin(), levels.end(), file.priority) - levels.begin()));
  }
  return result;
}

} // namespace biglybtc::compat
