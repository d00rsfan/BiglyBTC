// SPDX-License-Identifier: GPL-2.0-only
#include "local_rating.hpp"
#include <cstdlib>
#include <iostream>
using namespace biglybtc::plugins::rating;
void require(bool value) { if (!value) { std::abort(); } }
int main() {
  LocalRatingInputs input{std::u16string(maximumLocalTextUnits,u'0'),u"comment",u"nick"};
  const auto original = loadLocalRating(input);
  require(original && original->value.score == 0 && !original->invalidScoreText);
  input.rating->push_back(u'0');
  const auto oversized = loadLocalRating(input);
  require(!oversized && oversized.error() == LocalPolicyError::Limit);
  require(input.rating->size() == maximumLocalTextUnits + 1 && original->value.comment == Text{u"comment"});
  input.rating = u"3";
  for (auto* target : {&input.rating,&input.comment,&input.nickname}) {
    const auto retained = *target;
    *target = std::u16string(maximumLocalTextUnits+1,u'x');
    const auto rejected = planLocalRatingStore(input,RatingData::create(5,u"nick",u"new"));
    require(!rejected && rejected.error() == LocalPolicyError::Limit);
    require((*target)->size() == maximumLocalTextUnits+1);
    *target = retained;
  }
  for (bool nickname : {false,true}) {
    auto replacement = RatingData::create(3,u"nick",u"comment");
    (nickname ? replacement.nick : replacement.comment) = std::u16string(maximumLocalTextUnits+1,u'x');
    const auto rejected = planLocalRatingStore(input,replacement);
    require(!rejected && rejected.error() == LocalPolicyError::Limit);
    require(input.rating == Text{u"3"} && input.comment == Text{u"comment"} && input.nickname == Text{u"nick"});
  }
  auto replacement = RatingData::create(5,u"next",std::nullopt);
  auto plan = planLocalRatingStore(input,replacement);
  require(plan && !plan->failure && plan->publishUrgently && plan->writes.size() == 2);
  input = {}; replacement = {};
  auto owned = std::move(*plan);
  require(owned.previous.value.nick == Text{u"nick"} && owned.previous.value.comment == Text{u"comment"});
  require(owned.writes[0].attribute == LocalAttribute::Rating && owned.writes[0].value == Text{u"5"});
  require(owned.writes[1].attribute == LocalAttribute::Comment && !owned.writes[1].value);
  std::cout << "PASS local policy limits before intentions, caller input preservation and owned moved plans\n";
}
