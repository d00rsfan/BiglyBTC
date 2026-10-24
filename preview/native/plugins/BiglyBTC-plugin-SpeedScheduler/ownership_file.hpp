#pragma once

#include "scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace biglybtc::plugins::speed_scheduler {

// Original Formatters.formatByteArray(hash, false): uppercase hex, a space
// between groups of four bytes. PIF torrent identities supplied here are 20/32 bytes.
std::expected<std::string, Error> originalOwnershipHash(std::span<const std::uint8_t> hash);

class OwnershipFile {
public:
  static constexpr std::size_t MaximumBytes = 8 * 1024 * 1024;
  static constexpr std::size_t MaximumLines = 100000;
  static std::expected<OwnershipFile, Error> read(std::string bytes);
  const std::string& bytes() const { return bytes_; }
  std::expected<bool, Error> contains(std::span<const std::uint8_t> hash) const;
  std::expected<std::string, Error> afterPause(std::span<const std::uint8_t> hash,
      bool windowsNewline = false) const;
  std::expected<std::string, Error> afterResume(std::span<const std::uint8_t> hash) const;

private:
  struct Line { std::size_t start, end; };
  std::string bytes_;
  std::size_t lineCount_ = 0;
  std::map<std::string, Line, std::less<>> owned_;
  std::optional<Line> first(std::string_view hash) const;
};

} // namespace biglybtc::plugins::speed_scheduler
