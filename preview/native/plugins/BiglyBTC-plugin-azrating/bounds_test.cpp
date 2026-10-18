// SPDX-License-Identifier: GPL-2.0-only
#include "rating_data.hpp"
#include <cstdlib>
#include <iostream>
using namespace biglybtc::plugins::rating;
void require(bool value) { if (!value) { std::abort(); } }
int main() {
  auto original = decode("d1:c1:x1:n1:n1:si4e7:unknown3:abce");
  require(original.has_value() && original->complete());
  auto moved = std::move(*original);
  require(moved.document.serialize() == "d1:c1:x1:n1:n1:si4e7:unknown3:abce");
  require(moved.document.find("unknown")->bytes == "abc");
  auto partial = decode("d1:n1:n1:si4ee");
  require(partial.has_value() && partial->stage == DecodeStage::Comment && !partial->complete());
  require(!partial->value.encode());
  for (const auto input : {"d1:si1e1:si2ee", "d1:si01ee", "d1:siee", "d1:sli1e", "d1:si1eejunk"}) {
    require(!decode(input));
  }
  require(!decode(std::string(1024 * 1024 + 1, 'x')));
  require(!decode("d1:c" + std::string(40, 'l') + std::string(40, 'e') + "e"));
  RatingResults counts;
  const auto empty = RatingData::create(4, u"n", u"");
  for (std::size_t i = 0; i < RatingResults::maximumRatings; ++i) { require(counts.add(empty, false).has_value()); }
  require(counts.count() == RatingResults::maximumRatings && counts.average() == 4.0f);
  require(counts.add(empty, true).error() == Error::Limit);
  require(!counts.retained() && counts.comments().empty());
  RatingResults comments;
  auto tooLong = RatingData::create(5, u"n", std::u16string(RatingResults::maximumCommentUnits, u'x'));
  require(comments.add(tooLong, true).error() == Error::Limit);
  require(comments.count() == 0 && !comments.retained() && comments.comments().empty());
  RatingResults retention;
  auto nick = RatingData::create(5, std::u16string(RatingResults::maximumRetainedUnits, u'n'), u"");
  require(retention.add(nick, true).has_value());
  require(retention.add(empty, true).error() == Error::Limit);
  require(retention.count() == 1 && retention.retained()->size() == 1);
  require(retention.add(empty, false).has_value());
  require(retention.count() == 2 && retention.retained()->size() == 1);
  std::cout << "PASS bounded rating decode, immutable moved documents, partial-state errors and atomic aggregation limits\n";
}
