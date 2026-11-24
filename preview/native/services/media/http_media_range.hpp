// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::media {

enum class MediaHttpMethod { Get, Head };
enum class MediaRangeError { Malformed, Multiple, TooLong };

struct MediaHttpRange {
  enum class Status { Whole = 200, Partial = 206, Unsatisfiable = 416 };

  Status status;
  std::uint64_t fileSize;
  std::uint64_t offset;
  std::uint64_t length;
  bool sendBody;

  std::optional<std::string> contentRange() const;
};

// Plans a single range after the resource and its immutable size are selected.
std::expected<MediaHttpRange, MediaRangeError> planMediaHttpRange(
    MediaHttpMethod, std::uint64_t fileSize, std::optional<std::string_view> range);

} // namespace biglybtc::media
