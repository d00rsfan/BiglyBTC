// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_http_fetch.hpp"
#include "subscription_values.hpp"
#include <functional>

namespace biglybtc::search {
enum class SubscriptionServiceMode {ReadOnlyPreview,Normal};
enum class SubscriptionServiceError {Source,WrongWorker,Unsupported,Busy,Cancelled,Limit,PendingWrite,Memory};
enum class SubscriptionResultAction {MarkRead,MarkUnread,Delete};
struct SubscriptionCheckOutcome {
  SavedSubscriptionSelector selected;
  bool stored=false,cancelled=false;
  std::optional<SubscriptionServiceError> failure;
};
// A concrete provider remains mandatory even in tests. Its private constructors
// and actual source factories prevent fake completion/permission implementations.
using SubscriptionFetchFactory=std::function<std::unique_ptr<RssHttpFetch>(
    const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
    const SavedSubscriptionSelector&,std::stop_token)>;
class SavedSubscriptionService final {
public:
  static std::expected<std::unique_ptr<SavedSubscriptionService>,SubscriptionServiceError> open(
      const storage::ProfileLease&,std::filesystem::path,std::uint64_t,
      SubscriptionServiceMode,SubscriptionFetchFactory={});
  ~SavedSubscriptionService();
  std::expected<std::vector<SubscriptionSavedRow>,SubscriptionServiceError> catalog(
      const storage::ProfileLease&,std::uint64_t);
  std::expected<SubscriptionResults,SubscriptionServiceError> results(
      const storage::ProfileLease&,std::uint64_t,SavedSubscriptionSelector);
  std::expected<void,SubscriptionServiceError> check(
      const storage::ProfileLease&,std::uint64_t,SavedSubscriptionSelector,std::stop_token receiver={});
  std::expected<void,SubscriptionServiceError> changeResultState(
      const storage::ProfileLease&,std::uint64_t,SavedSubscriptionSelector,
      const std::vector<std::string>& exactKeys,SubscriptionResultAction);
  // Called only from the existing Host idle continuation. Due checks never run
  // in preview and use the same actual source/transport/store as manual checks.
  std::expected<std::optional<SubscriptionCheckOutcome>,SubscriptionServiceError> poll(
      const storage::ProfileLease&,std::uint64_t);
  // Worker-only. GUI cancellation uses the shared receiver stop token.
  void cancel()noexcept;
  std::expected<void,SubscriptionServiceError> close();
  bool active()const noexcept;
private:
  std::expected<void,SubscriptionServiceError> beginCheck(const storage::ProfileLease&,std::uint64_t,SavedSubscriptionSelector,std::stop_token,bool manual);
  struct Impl;explicit SavedSubscriptionService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
