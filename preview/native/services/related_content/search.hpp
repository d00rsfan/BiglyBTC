// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "related_content.hpp"
#include <chrono>
#include <memory>
namespace biglybtc::services::related_content {
struct SearchRequest {
  std::string term;
  std::int32_t minimumSeeds=-1,minimumLeechers=-1,maximumAgeSeconds=-1;
};
// Immutable query only. Matching confers no profile/publication/network rights.
class SearchQuery final {
public:
  static Result<SearchQuery> compile(SearchRequest);
  std::string_view normalisedTerm()const noexcept;
  bool popular()const noexcept;
  Result<bool> matches(const Item&,std::int64_t nowMillis)const;
private:
  struct Impl;explicit SearchQuery(std::shared_ptr<const Impl>);
  std::shared_ptr<const Impl> impl_;
};
// Original remote cap30/local popularity cap50; regular local results use the
// supplied bound. Hard work limits report unavailability, never guessed hits.
Result<std::vector<Item>> search(std::span<const Item>,const SearchQuery&,
    bool remote,std::int64_t nowMillis,std::size_t maximum=MaximumItems,
    std::chrono::milliseconds budget=std::chrono::milliseconds{100});
}
