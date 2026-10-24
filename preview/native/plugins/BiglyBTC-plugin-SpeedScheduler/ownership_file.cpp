#include "ownership_file.hpp"

namespace biglybtc::plugins::speed_scheduler {
namespace {
std::string_view javaTrim(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20) { value.remove_prefix(1); }
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20) { value.remove_suffix(1); }
  return value;
}
bool canonicalHash(std::string_view value) {
  if (value.size() != 44 && value.size() != 71) { return false; }
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i % 9 == 8) { if (value[i] != ' ') { return false; } }
    else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'A' && value[i] <= 'F'))) { return false; }
  }
  return true;
}
}

std::expected<std::string, Error> originalOwnershipHash(std::span<const std::uint8_t> hash) {
  if (hash.size() != 20 && hash.size() != 32) { return std::unexpected(Error{"Unsupported torrent ownership hash length"}); }
  constexpr std::string_view hex = "0123456789ABCDEF";
  std::string result;
  result.reserve(hash.size() * 2 + (hash.size() - 1) / 4);
  for (std::size_t i = 0; i < hash.size(); ++i) {
    if (i && i % 4 == 0) { result.push_back(' '); }
    result.push_back(hex[hash[i] >> 4]);
    result.push_back(hex[hash[i] & 15]);
  }
  return result;
}

std::expected<OwnershipFile, Error> OwnershipFile::read(std::string bytes) {
  if (bytes.size() > MaximumBytes) { return std::unexpected(Error{"Original pause ownership file exceeds its byte bound"}); }
  OwnershipFile result;
  result.bytes_ = std::move(bytes);
  const auto& input = result.bytes_;
  std::size_t start = 0;
  while (start < input.size()) {
    if (result.lineCount_ == MaximumLines) { return std::unexpected(Error{"Original pause ownership file exceeds its line bound"}); }
    auto contentEnd = input.find_first_of("\r\n", start);
    if (contentEnd == std::string::npos) { contentEnd = input.size(); }
    auto end = contentEnd;
    if (end < input.size()) {
      const bool cr = input[end++] == '\r';
      if (cr && end < input.size() && input[end] == '\n') { ++end; }
    }
    const auto value = javaTrim(std::string_view(input).substr(start, contentEnd - start));
    if (canonicalHash(value)) { result.owned_.try_emplace(std::string(value), Line{start, end}); }
    ++result.lineCount_;
    start = end;
  }
  return result;
}
std::optional<OwnershipFile::Line> OwnershipFile::first(std::string_view hash) const {
  const auto found = owned_.find(hash);
  return found == owned_.end() ? std::nullopt : std::optional{found->second};
}
std::expected<bool, Error> OwnershipFile::contains(std::span<const std::uint8_t> hash) const {
  auto formatted = originalOwnershipHash(hash);
  if (!formatted) { return std::unexpected(formatted.error()); }
  return first(*formatted).has_value();
}
std::expected<std::string, Error> OwnershipFile::afterPause(std::span<const std::uint8_t> hash, bool windowsNewline) const {
  auto formatted = originalOwnershipHash(hash);
  if (!formatted) { return std::unexpected(formatted.error()); }
  if (first(*formatted)) { return bytes_; }
  // Original append merges an unterminated final line with the new hash. Do not
  // acknowledge a pause ownership write that Java would be unable to find.
  if (!bytes_.empty() && bytes_.back() != '\r' && bytes_.back() != '\n') {
    return std::unexpected(Error{"Cannot append ownership after an unterminated original line"});
  }
  const std::string_view ending = windowsNewline ? "\r\n" : "\n";
  if (lineCount_ == MaximumLines || formatted->size() + ending.size() > MaximumBytes - bytes_.size()) {
    return std::unexpected(Error{"Pause ownership append exceeds its bound"});
  }
  return bytes_ + *formatted + std::string(ending);
}
std::expected<std::string, Error> OwnershipFile::afterResume(std::span<const std::uint8_t> hash) const {
  auto formatted = originalOwnershipHash(hash);
  if (!formatted) { return std::unexpected(formatted.error()); }
  const auto line = first(*formatted);
  if (!line) { return bytes_; }
  // Java removes only the first matching record. Preserve all other raw bytes,
  // including unknown/blank lines and their original line endings.
  auto result = bytes_;
  result.erase(line->start, line->end - line->start);
  return result;
}
} // namespace biglybtc::plugins::speed_scheduler
