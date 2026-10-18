// SPDX-License-Identifier: GPL-2.0-only
// Port of bounded RatingData/RatingResults/KeyGenUtils behavior from
// 1629340340d0bc391d9e582f9ee989a93d550581, Copyright (C) 2004 Aelitis SARL.
#include "rating_data.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>
namespace biglybtc::plugins::rating {
namespace {
constexpr std::size_t maximumBytes = 1024 * 1024;
bool continuation(unsigned char byte) { return (byte & 0xc0) == 0x80; }
std::u16string javaUtf8(std::string_view bytes) {
  std::u16string result;
  for (std::size_t i = 0; i < bytes.size();) {
    const auto first = static_cast<unsigned char>(bytes[i]);
    unsigned length = 1;
    std::uint32_t code = first;
    if (first < 128) { result += static_cast<char16_t>(first); ++i; continue; }
    if (first >= 0xc2 && first <= 0xdf) { length = 2; code &= 31; }
    else if (first >= 0xe0 && first <= 0xef) { length = 3; code &= 15; }
    else if (first >= 0xf0 && first <= 0xf4) { length = 4; code &= 7; }
    else { result += u'\ufffd'; ++i; continue; }
    unsigned consumed = 1;
    for (; consumed < length && i + consumed < bytes.size(); ++consumed) {
      const auto next = static_cast<unsigned char>(bytes[i + consumed]);
      if (!continuation(next)) { break; }
      if (consumed == 1 && ((first == 0xe0 && next < 0xa0)
          || (first == 0xf0 && next < 0x90) || (first == 0xf4 && next > 0x8f))) { break; }
      code = (code << 6) | (next & 63);
    }
    if (consumed != length || (code >= 0xd800 && code <= 0xdfff)) { result += u'\ufffd'; }
    else if (code <= 0xffff) { result += static_cast<char16_t>(code); }
    else {
      code -= 0x10000;
      result += static_cast<char16_t>(0xd800 + (code >> 10));
      result += static_cast<char16_t>(0xdc00 + (code & 1023));
    }
    i += consumed;
  }
  return result;
}
std::string utf8(std::u16string_view text) {
  std::string output;
  for (std::size_t i = 0; i < text.size(); ++i) {
    std::uint32_t code = text[i];
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.size()
        && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
      code = 0x10000 + ((code - 0xd800) << 10) + (text[++i] - 0xdc00);
    } else if (code >= 0xd800 && code <= 0xdfff) { code = '?'; }
    if (code < 128) { output += static_cast<char>(code); }
    else if (code < 0x800) {
      output += static_cast<char>(0xc0 | (code >> 6)); output += static_cast<char>(0x80 | (code & 63));
    } else {
      if (code >= 0x10000) { output += static_cast<char>(0xf0 | (code >> 18)); output += static_cast<char>(0x80 | ((code >> 12) & 63)); }
      else { output += static_cast<char>(0xe0 | (code >> 12)); }
      output += static_cast<char>(0x80 | ((code >> 6) & 63)); output += static_cast<char>(0x80 | (code & 63));
    }
  }
  return output;
}
std::string bstring(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::int32_t javaInt(double value) {
  if (std::isnan(value)) { return 0; }
  if (value >= std::numeric_limits<std::int32_t>::max()) { return std::numeric_limits<std::int32_t>::max(); }
  if (value <= std::numeric_limits<std::int32_t>::min()) { return std::numeric_limits<std::int32_t>::min(); }
  return static_cast<std::int32_t>(value);
}
}
RatingData RatingData::create(std::int32_t score, Text nick, Text comment) {
  return {std::min(5, score), std::move(nick), std::move(comment)};
}
std::expected<std::string, Error> RatingData::encode() const {
  if (!comment) { return std::unexpected(Error::NullComment); }
  if (!comment->empty() && !nick) { return std::unexpected(Error::NullNick); }
  // Original BEncoder.intToBytes negates int32 MIN then throws; encodes catches
  // that failure and returns no bytes. Do not manufacture a publishable value.
  if (score == std::numeric_limits<std::int32_t>::min()) { return std::unexpected(Error::OriginalIntegerEncoding); }
  if (comment->size() > maximumBytes || (nick && nick->size() > maximumBytes)) { return std::unexpected(Error::Limit); }
  const auto c = utf8(*comment);
  const auto n = comment->empty() ? std::string{} : utf8(*nick);
  if (c.size() + n.size() > maximumBytes - 64) { return std::unexpected(Error::Limit); }
  return "d1:c" + bstring(c) + "1:n" + bstring(n) + "1:si" + std::to_string(score) + "ee";
}
std::expected<DecodedRating, Error> decode(std::string bytes) {
  auto document = core::Document::parse(std::move(bytes),
      {.inputBytes=maximumBytes, .stringBytes=maximumBytes, .nodes=10000, .depth=32});
  if (!document) { return std::unexpected(Error::InvalidBencode); }
  DecodedRating result{std::move(*document), {}, DecodeStage::Score};
  const auto* score = result.document.find("s");
  if (!score || score->type != core::Node::Type::Integer) { return result; }
  result.value.score = std::min(5, std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(score->integer)));
  result.stage = DecodeStage::Nick;
  const auto* nick = result.document.find("n");
  if (!nick || nick->type != core::Node::Type::Bytes) { return result; }
  result.value.nick = javaUtf8(nick->bytes);
  result.stage = DecodeStage::Comment;
  const auto* comment = result.document.find("c");
  if (!comment || comment->type != core::Node::Type::Bytes) { return result; }
  result.value.comment = javaUtf8(comment->bytes);
  result.stage = DecodeStage::Complete;
  return result;
}
std::expected<std::array<std::uint8_t, 27>, Error> lookupKey(std::span<const std::uint8_t> hash) {
  if (hash.size() < 20) { return std::unexpected(Error::InvalidHash); }
  std::array<std::uint8_t, 27> result{};
  std::copy_n(hash.begin(), 20, result.begin());
  constexpr std::string_view suffix = ".rating";
  std::copy(suffix.begin(), suffix.end(), result.begin() + 20);
  return result;
}
std::expected<void, Error> RatingResults::add(const RatingData& rating, bool retain) {
  if (rating.score <= 0) { return {}; }
  // Limits reject without mutation. Original null-comment exceptions occur after
  // the average/retention update; preserve that observable partial state.
  const auto nickLength = rating.nick ? rating.nick->size() : 4;
  const auto retainedNickLength = rating.nick ? rating.nick->size() : 0;
  const auto retainedCommentLength = rating.comment ? rating.comment->size() : 0;
  if (retain && (retainedNickLength > maximumRetainedUnits || retainedCommentLength > maximumRetainedUnits
      || retainedUnits_ + retainedNickLength + retainedCommentLength > maximumRetainedUnits)) {
    return std::unexpected(Error::Limit);
  }
  if (count_ == maximumRatings || (rating.comment && !rating.comment->empty()
      && (rating.comment->size() > maximumCommentUnits || nickLength > maximumCommentUnits
      || comments_.size() + rating.comment->size() + nickLength + 64 > maximumCommentUnits))) {
    return std::unexpected(Error::Limit);
  }
  if (retain) {
    if (!retained_) { retained_.emplace(); }
    retained_->push_back(rating);
    retainedUnits_ += retainedNickLength + retainedCommentLength;
  }
  const float total = average_ * static_cast<float>(count_) + static_cast<float>(rating.score);
  ++count_;
  average_ = total / static_cast<float>(count_);
  if (!rating.comment) { return std::unexpected(Error::NullComment); }
  if (!rating.comment->empty()) {
    ++commentCount_;
    comments_ += rating.nick ? *rating.nick : u"null";
    comments_ += u" :\n";
    const auto score = std::to_string(rating.score);
    comments_.append(score.begin(), score.end());
    comments_ += u"/5 . "; comments_ += *rating.comment; comments_ += u"\n====================\n";
  }
  return {};
}
std::int32_t RatingResults::roundedAverage() const { return javaInt(static_cast<double>(average_) + 0.5); }
std::string RatingResults::averageText() const {
  const auto average = javaInt(average_ * 10.0f);
  return std::to_string(average / 10) + '.' + std::to_string(average % 10);
}
}
