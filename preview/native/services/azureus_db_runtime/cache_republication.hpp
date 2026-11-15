// SPDX-License-Identifier: GPL-2.0-or-later
// Selection in original DHTDBImpl.republishCachedMappings, core cb8c6cb9.
// This pure projection grants no source, routing, storage or network authority.
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
namespace biglybtc::services::azureus_db_runtime {
struct CachedValueAge {bool local=false;std::int8_t replication=-1;std::int64_t stored=0;};
enum class CachedSelectionError {Invalid,Capacity};
struct CachedSelection {
 bool skipped=false,surveyOnly=false;
 std::uint16_t dueCount=0,rollbackCount=0;
 std::array<std::uint16_t,256> due{},rollback{};
 std::span<const std::uint16_t> dueValues()const noexcept{return std::span(due).first(dueCount);}
 std::span<const std::uint16_t> clockRollbacks()const noexcept{return std::span(rollback).first(rollbackCount);}
};
inline constexpr std::expected<CachedSelection,CachedSelectionError> selectCachedValues(
 std::int8_t diversification,std::span<const CachedValueAge> values,
 std::int64_t now,std::int64_t period)noexcept{
 if(diversification<1||diversification>3||period<0||period>std::numeric_limits<std::int32_t>::max()){return std::unexpected(CachedSelectionError::Invalid);}
 if(values.size()>256){return std::unexpected(CachedSelectionError::Capacity);}
 CachedSelection out;if(diversification!=1){out.skipped=true;return out;}
 out.surveyOnly=!values.empty();
 for(std::size_t i=0;i<values.size();++i){
  const auto& value=values[i];if(value.local){out.surveyOnly=false;continue;}
  if(value.replication==-1){out.surveyOnly=false;}
  if(now<value.stored){out.rollback[out.rollbackCount++]=static_cast<std::uint16_t>(i);continue;}
  // Java long subtraction wraps even after its signed future-time comparison.
  const auto age=std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(now)-std::bit_cast<std::uint64_t>(value.stored));
  if(age>period){out.due[out.dueCount++]=static_cast<std::uint16_t>(i);}
 }
 // Explicit-replication-only mappings are surveyed; forwarding them normally
 // would change the original replication strategy even when survey is disabled.
 if(out.surveyOnly){out.dueCount=0;}
 return out;
}
}
