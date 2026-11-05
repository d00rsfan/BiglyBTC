// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../unique32.hpp"
#include <algorithm>
#include <limits>
#include <span>
#include <utility>

namespace biglybtc::services::protocol_ids::detail {
// Private value-only machinery, not an allocator factory or authority hook.
// The width/round parameters allow exhaustive small-domain tests of precisely
// the round loop used by the fixed 32-bit, 1024-round production prototype.
inline constexpr std::uint32_t mask(unsigned width) noexcept {
  return width==32?std::numeric_limits<std::uint32_t>::max():
      width>0&&width<32?(std::uint32_t{1}<<width)-1:0;
}
template<class Coin>
std::expected<std::uint32_t,Error> transform(std::uint32_t value,unsigned width,
    std::span<const std::uint32_t> pivots,Coin&& coin,bool inverse=false) noexcept {
  const auto domain=mask(width);
  if(width==0||width>32||value>domain||pivots.empty()||pivots.size()>Rounds){return std::unexpected(Error::Failed);}
  for(const auto pivot:pivots){if(pivot>domain){return std::unexpected(Error::Failed);}}
  for(std::size_t step=0;step<pivots.size();++step){
    const auto round=inverse?pivots.size()-1-step:step;
    const auto partner=value^pivots[round];
    const auto choose=std::uint32_t{0}-static_cast<std::uint32_t>(value<partner);
    const auto canonical=value^((value^partner)&choose);
    const auto bit=coin(round,canonical);
    if(!bit){return std::unexpected(bit.error());}
    value^=pivots[round]&(std::uint32_t{0}-static_cast<std::uint32_t>(*bit));
  }
  return value;
}

// Testable arithmetic only. An internal caller cannot make a public Unique32
// start at a chosen value; Unique32 always starts at zero with a fresh secret.
inline std::expected<std::uint32_t,Error> reserve(std::uint64_t& cursor,
    std::uint64_t limit=MaximumInputs) noexcept {
  if(limit>MaximumInputs||cursor>=limit){return std::unexpected(Error::Exhausted);}
  const auto value=static_cast<std::uint32_t>(cursor);++cursor;return value;
}
class Sequence final {
  const std::uint64_t limit_;
  std::uint64_t cursor_=0;
  bool failed_=false;
public:
  explicit Sequence(std::uint64_t limit=MaximumInputs) noexcept:limit_(limit){}
  Sequence(const Sequence&)=delete;Sequence& operator=(const Sequence&)=delete;
  std::uint64_t consumed() const noexcept{return cursor_;}
  bool failed() const noexcept{return failed_;}
  template<class Evaluate>
  std::expected<std::uint32_t,Error> next(Zero zero,Evaluate&& evaluate) noexcept {
    if(failed_){return std::unexpected(Error::Failed);}
    for(;;){
      const auto input=reserve(cursor_,limit_);if(!input){return std::unexpected(input.error());}
      auto output=evaluate(*input);
      if(!output){failed_=true;return std::unexpected(output.error());}
      if(zero==Zero::Allow||*output!=0){return output;}
    }
  }
};
} // namespace biglybtc::services::protocol_ids::detail
