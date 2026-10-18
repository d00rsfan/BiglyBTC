// SPDX-License-Identifier: GPL-2.0-only
// Original updater: Copyright (C) 2004 Aelitis SARL.
#include "local_rating.hpp"
#include <algorithm>
#include <array>
namespace biglybtc::plugins::rating {
namespace {
bool bounded(const Text& text) { return !text || text->size() <= maximumLocalTextUnits; }
// Character.digit(char,10) observed exhaustively with the qualified Java17
// reference. Supplementary digits are surrogate pairs and fail parseInt(String).
constexpr std::array<char16_t, 37> decimalZeroes{
  0x0030,0x0660,0x06f0,0x07c0,0x0966,0x09e6,0x0a66,0x0ae6,0x0b66,0x0be6,
  0x0c66,0x0ce6,0x0d66,0x0de6,0x0e50,0x0ed0,0x0f20,0x1040,0x1090,0x17e0,
  0x1810,0x1946,0x19d0,0x1a80,0x1a90,0x1b50,0x1bb0,0x1c40,0x1c50,0xa620,
  0xa8d0,0xa900,0xa9d0,0xa9f0,0xaa50,0xabf0,0xff10};
int digit(char16_t unit) {
  auto found = std::upper_bound(decimalZeroes.begin(), decimalZeroes.end(), unit);
  if (found == decimalZeroes.begin()) { return -1; }
  const auto value = unit - *--found;
  return value < 10 ? value : -1;
}
std::optional<std::int32_t> parseInteger(std::u16string_view text) {
  if (text.empty()) { return {}; }
  const bool negative = text.front() == u'-';
  std::size_t offset = negative || text.front() == u'+' ? 1 : 0;
  if (offset == text.size()) { return {}; }
  const std::int64_t limit = negative ? 2147483648LL : 2147483647LL;
  std::int64_t value = 0;
  for (; offset < text.size(); ++offset) {
    const auto next = digit(text[offset]);
    if (next < 0 || value > (limit - next) / 10) { return {}; }
    value = value * 10 + next;
  }
  return static_cast<std::int32_t>(negative ? -value : value);
}
}
std::expected<LocalRating, LocalPolicyError> loadLocalRating(const LocalRatingInputs& input) {
  if (!bounded(input.rating) || !bounded(input.comment) || !bounded(input.nickname)) {
    return std::unexpected(LocalPolicyError::Limit);
  }
  const auto score = input.rating ? parseInteger(*input.rating) : std::optional<std::int32_t>{0};
  return LocalRating{RatingData::create(score.value_or(0), input.nickname, input.comment.value_or(u"")), !score};
}
std::expected<LocalRatingStorePlan, LocalPolicyError> planLocalRatingStore(
    const LocalRatingInputs& input, const RatingData& replacement) {
  if (!bounded(replacement.nick) || !bounded(replacement.comment)) { return std::unexpected(LocalPolicyError::Limit); }
  auto previous = loadLocalRating(input);
  if (!previous) { return std::unexpected(previous.error()); }
  LocalRatingStorePlan plan{std::move(*previous), {}, false, {}};
  bool update = plan.previous.value.score != replacement.score || plan.previous.value.comment != replacement.comment;
  if (update) {
    const auto score = std::to_string(replacement.score);
    plan.writes.push_back({LocalAttribute::Rating, std::u16string(score.begin(), score.end())});
    plan.writes.push_back({LocalAttribute::Comment, replacement.comment});
  }
  if (!plan.previous.value.nick) { plan.failure = LocalStoreFailure::NullNickname; return plan; }
  update |= plan.previous.value.nick != replacement.nick;
  plan.publishUrgently = update;
  return plan;
}
}
