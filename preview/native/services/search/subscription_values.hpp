// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_subscription_source.hpp"
#include <map>

namespace biglybtc::search {
enum class SubscriptionValueError {Malformed,Ambiguous,Unsupported,Limit,Memory};
struct SubscriptionSavedRow {
  SavedSubscriptionSelector selected;
  QString name;
  bool subscribed=false,enabled=true,autoDownload=false,anonymous=false;
  std::int64_t lastScan=0,lastNew=0,intervalOverride=0,maxResults=-1;
  std::int64_t intervalMinutes=0,defaultCheckMinutes=120,readCount=0,unreadCount=0;
  std::optional<SubscriptionValueError> issue;
};
struct SubscriptionIndex {
  std::shared_ptr<const core::Document> source;
  bool markLibraryRead=true;
  std::vector<SubscriptionSavedRow> rows;
};
struct SubscriptionNumericFilter;
struct SubscriptionDefinition {
  std::int64_t engineId=0,intervalMinutes=0;
  bool embedded=false;
  std::shared_ptr<const core::Document> engine;
  std::shared_ptr<const SubscriptionNumericFilter> numericFilter;
};
struct SubscriptionResultValue {
  std::string key;
  std::optional<std::string> secondaryKey;
  bool read=false,deleted=false;
  std::int64_t deletedLastSeenDay=0,timeFoundSeconds=0;
  // Original encoded row/JSON retained. Unknown values are never reconstructed
  // from a reduced UI projection. These owning bytes must not be logged.
  std::string encoded,json;
};
struct SubscriptionResults {
  std::shared_ptr<const core::Document> source;
  std::vector<SubscriptionResultValue> rows;
};
struct SubscriptionHistoryCommit {
  std::string results,configuration;
  std::int64_t scanMillis=0;
};
// Pure bounded codecs/policy only; no profile, effect, authority or network.
std::expected<SubscriptionIndex,SubscriptionValueError> parseSubscriptionIndex(std::string);
std::expected<SubscriptionDefinition,SubscriptionValueError> parseSubscriptionDefinition(
    std::string body,const SubscriptionSavedRow&,const core::Document* actualCatalog);
std::expected<SubscriptionResults,SubscriptionValueError> parseSubscriptionResults(std::string);
std::expected<SubscriptionHistoryCommit,SubscriptionValueError> reconcileSubscriptionResults(
    const SubscriptionIndex&,std::size_t,const SubscriptionResults&,const RssDocument&,
    std::int64_t engineId,std::int64_t nowMillis,const SubscriptionNumericFilter* numericFilter=nullptr);
std::expected<SubscriptionHistoryCommit,SubscriptionValueError> changeSubscriptionResultState(
    const SubscriptionIndex&,std::size_t,const SubscriptionResults&,
    const std::vector<std::string>& exactKeys,bool read,bool deleted,std::int64_t nowMillis);
std::int64_t subscriptionNextScan(std::int64_t lastScan,std::int64_t overrideMinutes,
    std::int64_t intervalMinutes,std::int64_t configuredDefaultMinutes,std::int64_t nowMillis)noexcept;
std::int64_t subscriptionRetryTime(std::int64_t failureMillis,unsigned consecutive)noexcept;
} // namespace biglybtc::search
