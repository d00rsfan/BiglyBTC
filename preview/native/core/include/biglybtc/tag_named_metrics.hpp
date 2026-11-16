// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_registry.hpp"
#include <span>
namespace biglybtc::core::tags::constraints {
struct NamedTagMetric {
 std::int64_t uid=0;
 std::optional<std::int32_t> count,queuePosition,addedPosition;
 std::optional<std::int64_t> addedMilliseconds;
};
struct NamedTagMember {
 std::uint64_t identity=0;std::int32_t taggableType=2;
 // Observed original DM.getPosition(),not a display row or inferred zero.
 std::optional<std::int32_t> position;
 std::optional<std::int64_t> addedMilliseconds;
};
struct NamedMetricLimits {std::size_t entries=4096,members=4096;};
// Pure owning data for ONE observed subject. It grants no registry census,
// provider,tag writer,clock or effect authority. Incomplete counts/ranks remain
// absent; count includes all taggable types,positions require downloads.
std::expected<NamedTagMetric,runtime::Error> namedMetricFromMembers(
 std::int64_t uid,std::span<const NamedTagMember>,std::optional<std::uint64_t> subject,
 bool complete,NamedMetricLimits={});
class NamedTagMetrics final {
public:
 static std::expected<NamedTagMetrics,runtime::Error> create(std::vector<NamedTagMetric>,NamedMetricLimits={});
 const NamedTagMetric* find(std::int64_t uid)const noexcept;
 std::size_t size()const noexcept{return entries_.size();}
private:
 explicit NamedTagMetrics(std::vector<NamedTagMetric> values):entries_(std::move(values)){}
 std::vector<NamedTagMetric> entries_;
};
}
