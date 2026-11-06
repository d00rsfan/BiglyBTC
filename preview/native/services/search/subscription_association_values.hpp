// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_values.hpp"
#include <array>
#include <cstddef>
namespace biglybtc::search::detail {
using SubscriptionAssociationHash=std::array<std::byte,20>;
struct SubscriptionAssociationValue {SubscriptionAssociationHash hash{};std::int64_t when=0;std::string encoded;};
struct SubscriptionAssociationValues {
  std::shared_ptr<const core::Document> metadata;
  std::optional<std::string> originalList;
  std::vector<SubscriptionAssociationValue> rows;
};
enum class SubscriptionAssociationCapacity {AppendWithoutEviction,AlreadyPresent,OriginalRandomEvictionRequired};
struct SubscriptionAssociationValueChange {std::string encodedList;bool changed=false;};
// Internal PURE values only: no lease, source permission, store or mutator.
// Only a later private coordinator may bind these bytes to proven intake.
std::expected<SubscriptionAssociationValues,SubscriptionValueError> parseSubscriptionAssociations(std::string actualMetadataRow);
SubscriptionAssociationCapacity subscriptionAssociationCapacity(const SubscriptionAssociationValues&,
    const std::optional<SubscriptionAssociationHash>& knownMetainfoHash={})noexcept;
std::expected<SubscriptionAssociationValueChange,SubscriptionValueError> planSubscriptionAssociationValue(
    const SubscriptionAssociationValues&,const SubscriptionAssociationHash&,std::int64_t actualObservedMillis);
} // namespace biglybtc::search::detail
