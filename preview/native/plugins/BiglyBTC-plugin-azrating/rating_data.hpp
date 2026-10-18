// SPDX-License-Identifier: GPL-2.0-only
// Original data classes: Copyright (C) 2004 Aelitis SARL.
#pragma once
#include "biglybtc/bencode.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::plugins::rating {
enum class Error { InvalidBencode, InvalidHash, Limit, NullNick, NullComment, OriginalIntegerEncoding };
using Text = std::optional<std::u16string>;
// UTF-16 retains the Java model, including NULs and unpaired surrogate inputs.
struct RatingData {
  std::int32_t score = 0;
  Text nick, comment;
  static RatingData create(std::int32_t score, Text nick, Text comment);
  bool needsPublishing() const { return score > 0; }
  std::expected<std::string, Error> encode() const;
};
enum class DecodeStage { Score, Nick, Comment, Complete };
struct DecodedRating {
  core::Document document; // Unknown fields and original bytes remain owned.
  RatingData value;
  DecodeStage stage = DecodeStage::Score;
  bool complete() const { return stage == DecodeStage::Complete; }
};
// A partially initialized original object is observable, but must never be
// published automatically. No I/O, attribute mutation or DDB service is here.
std::expected<DecodedRating, Error> decode(std::string bytes);
std::expected<std::array<std::uint8_t, 27>, Error> lookupKey(std::span<const std::uint8_t> hash);

// Single host owner thread, bounded storage. No background work or synchronization.
class RatingResults final {
public:
  static constexpr std::size_t maximumRatings = 100000, maximumCommentUnits = 1024 * 1024;
  static constexpr std::size_t maximumRetainedUnits = 4 * 1024 * 1024;
  std::expected<void, Error> add(const RatingData& rating, bool retain);
  void setAverage(float value) { average_ = value; }
  float average() const { return average_; }
  std::int32_t roundedAverage() const;
  std::string averageText() const;
  std::size_t count() const { return count_; }
  std::size_t commentCount() const { return commentCount_; }
  const std::u16string& comments() const { return comments_; }
  const std::optional<std::vector<RatingData>>& retained() const { return retained_; }
private:
  float average_ = 0;
  std::size_t count_ = 0, commentCount_ = 0;
  std::size_t retainedUnits_ = 0;
  std::u16string comments_;
  std::optional<std::vector<RatingData>> retained_;
};
}
