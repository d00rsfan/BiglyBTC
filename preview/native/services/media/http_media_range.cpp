// SPDX-License-Identifier: GPL-2.0-or-later
#include "http_media_range.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace biglybtc::media {
namespace {
std::string_view trim(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t");
  return begin == value.npos ? std::string_view{} :
      value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
}

bool tokenCharacter(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
      std::string_view("!#$%&'*+-.^_`|~").find(ch) != std::string_view::npos;
}

bool isBytesUnit(std::string_view unit) {
  return std::ranges::equal(unit, std::string_view("bytes"), [](char actual, char expected) {
    return actual == expected || actual == expected - 'a' + 'A';
  });
}

std::optional<std::string_view> decimal(std::string_view value) {
  if (value.empty() || !std::ranges::all_of(value, [](char ch) { return ch >= '0' && ch <= '9'; })) {
    return std::nullopt;
  }
  const auto nonZero = value.find_first_not_of('0');
  return nonZero == value.npos ? value.substr(0, 1) : value.substr(nonZero);
}

bool decimalLess(std::string_view left, std::string_view right) {
  return left.size() == right.size() ? left < right : left.size() < right.size();
}

std::uint64_t saturatedInteger(std::string_view value) {
  std::uint64_t parsed{};
  const auto parsedResult = std::from_chars(value.data(), value.data() + value.size(), parsed);
  return parsedResult.ec == std::errc::result_out_of_range ? std::numeric_limits<std::uint64_t>::max() : parsed;
}
} // namespace

std::optional<std::string> MediaHttpRange::contentRange() const {
  switch (status) {
  case Status::Whole:
    return std::nullopt;
  case Status::Partial:
    return "bytes " + std::to_string(offset) + '-' + std::to_string(offset + length - 1) + '/' +
        std::to_string(fileSize);
  case Status::Unsatisfiable:
    return "bytes */" + std::to_string(fileSize);
  }
  return std::nullopt;
}

std::expected<MediaHttpRange, MediaRangeError> planMediaHttpRange(
    MediaHttpMethod method, std::uint64_t size, std::optional<std::string_view> range) {
  using Status = MediaHttpRange::Status;
  const MediaHttpRange whole{Status::Whole, size, 0, size, method == MediaHttpMethod::Get && size > 0};
  if (method == MediaHttpMethod::Head || !range) {
    return whole;
  }
  if (range->size() > 256) {
    return std::unexpected(MediaRangeError::TooLong);
  }
  const auto value = trim(*range);
  const auto equals = value.find('=');
  const auto unit = value.substr(0, equals);
  if (equals == value.npos || unit.empty() || equals + 1 == value.size() ||
      !std::ranges::all_of(unit, tokenCharacter)) {
    return std::unexpected(MediaRangeError::Malformed);
  }
  if (!isBytesUnit(unit)) {
    return whole;
  }
  const auto spec = value.substr(equals + 1);
  if (spec.find(',') != spec.npos) {
    return std::unexpected(MediaRangeError::Multiple);
  }
  const auto dash = spec.find('-');
  if (dash == spec.npos || spec.size() == 1) {
    return std::unexpected(MediaRangeError::Malformed);
  }
  const MediaHttpRange unsatisfiable{Status::Unsatisfiable, size, 0, 0, false};
  if (dash == 0) {
    const auto suffix = decimal(spec.substr(1));
    if (!suffix) {
      return std::unexpected(MediaRangeError::Malformed);
    }
    const auto length = std::min(size, saturatedInteger(*suffix));
    return length == 0 ? unsatisfiable : MediaHttpRange{Status::Partial, size, size - length, length, true};
  }
  const auto first = decimal(spec.substr(0, dash));
  const auto rest = spec.substr(dash + 1);
  const auto last = decimal(rest);
  if (!first || (!rest.empty() && (!last || decimalLess(*last, *first)))) {
    return std::unexpected(MediaRangeError::Malformed);
  }
  const auto start = saturatedInteger(*first);
  if (start >= size) {
    return unsatisfiable;
  }
  const auto end = last ? std::min(size - 1, saturatedInteger(*last)) : size - 1;
  return MediaHttpRange{Status::Partial, size, start, end - start + 1, true};
}

} // namespace biglybtc::media
