// SPDX-License-Identifier: GPL-2.0-only
// Original updater: Copyright (C) 2004 Aelitis SARL.
#pragma once
#include "rating_data.hpp"
namespace biglybtc::plugins::rating {
struct LocalRatingInputs {
  Text rating, comment, nickname; // Supplied host attributes and current plugin nickname.
};
enum class LocalPolicyError { Limit };
inline constexpr std::size_t maximumLocalTextUnits = 1024 * 1024;
struct LocalRating {
  RatingData value;
  bool invalidScoreText = false; // Original catches parseInt failure and uses zero.
};
std::expected<LocalRating, LocalPolicyError> loadLocalRating(const LocalRatingInputs& input);
enum class LocalAttribute { Rating, Comment };
struct LocalAttributeWrite {
  LocalAttribute attribute;
  Text value; // null means the original setter receives null.
};
enum class LocalStoreFailure { NullNickname };
struct LocalRatingStorePlan {
  LocalRating previous;
  std::vector<LocalAttributeWrite> writes;
  bool publishUrgently = false;
  // The original can throw AFTER its ordered setters. Retain those intentions
  // and the failure; a future durable host executor must handle that boundary.
  std::optional<LocalStoreFailure> failure;
};
// Non-null original RatingData input. Pure preview, no identity binding, file
// writer, queue, timer or DDB access. Host eligibility must precede real publish.
std::expected<LocalRatingStorePlan, LocalPolicyError> planLocalRatingStore(
    const LocalRatingInputs& input, const RatingData& replacement);
}
