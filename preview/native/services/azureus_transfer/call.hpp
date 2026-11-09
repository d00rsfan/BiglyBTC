// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "data.hpp"
#include <optional>
#include <vector>
namespace biglybtc::services::azureus_transfer {
inline constexpr std::int64_t WriteResendMilliseconds=12500,ReadRetryMilliseconds=5000;
struct Range {std::int32_t start=0,length=0;bool operator==(const Range&)const=default;};
// Original TreeSet start ordering discards overlapping packets. Bounds and
// consistent totals are a finite safety subset; no allocation from raw total.
class Reassembly final {
public:
  std::expected<bool,Error> add(const Data&); // true only when the complete range exists.
  std::vector<Range> missing()const;
  std::optional<std::string> value()const;
  bool started()const noexcept{return total_.has_value();}
  std::size_t pieces()const noexcept{return pieces_.size();}
private:
  std::optional<std::int32_t> total_;std::vector<Data> pieces_;
};
std::expected<std::vector<Data>,Error> fragments(Type,std::string_view handler,std::string_view key,std::string_view whole,std::int32_t start=0,std::int32_t length=0,bool firstOnly=false);
struct WriteProgress {
  bool receivedAck=false,complete=false;
  bool acknowledge(const Data&,std::size_t total)noexcept;
};
}
