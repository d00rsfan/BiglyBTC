// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_rss_source.hpp"
#include <optional>

namespace biglybtc::search {
// Identity observed from the actual subscriptions.config list. shortId is the
// raw10-byte Original identity, not a display string or fabricated engine UID.
struct SavedSubscriptionSelector {
  std::size_t sourceIndex=0;
  std::string shortId,publicKey;
  std::optional<std::string> singletonKey;
  std::int64_t version=0;
  bool operator==(const SavedSubscriptionSelector&)const=default;
};
struct SubscriptionRssDescription : RssGetDescription {
  SavedSubscriptionSelector selected;
  std::int64_t engineId=0;
  std::string engineUid;
  bool embeddedEngine=false;
};
class SavedSubscriptionService;
class SavedSubscriptionResultSnapshot;
class RssHttpFetch;
namespace detail {struct SubscriptionAutomaticPolicyValues;}
struct SubscriptionNumericFilter;
struct SubscriptionLiteralFilter;
struct SubscriptionHistoryCommit;
struct SubscriptionSavedRow;
struct SubscriptionResults;
// Description only. The concrete transport itself reads this actual source;
// it never accepts a caller-created description, URL, grant or source callback.
// Actual policy selection/manual-vs-due and durable publication stay inside the
// serialized subscription service. Unsupported automatic acquisition is refused
// before fetch; it must not advance scan/read state on a switch back to Java.
class SavedSubscriptionSourceSnapshot final {
public:
  static std::expected<SavedSubscriptionSourceSnapshot,SavedRssSourceError> read(
      const storage::ProfileLease&,const std::filesystem::path& profileDirectory,
      std::uint64_t profileSession,SavedSubscriptionSelector);
  SavedSubscriptionSourceSnapshot(SavedSubscriptionSourceSnapshot&&)noexcept;
  SavedSubscriptionSourceSnapshot& operator=(SavedSubscriptionSourceSnapshot&&)noexcept;
  ~SavedSubscriptionSourceSnapshot();
  SavedSubscriptionSourceSnapshot(const SavedSubscriptionSourceSnapshot&)=delete;
  SavedSubscriptionSourceSnapshot& operator=(const SavedSubscriptionSourceSnapshot&)=delete;
  const SubscriptionRssDescription& description()const noexcept;
  // Creator worker, current session and actual SAME-OFD caller required. Owns
  // its brief unnested read gate, revalidates relevant named sources/aliases and
  // returns the fresh revision. Unrelated profile writes may be accepted after
  // exact relevant revalidation. Never hold an outer source gate around current.
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession)const;
private:
  friend class SavedSubscriptionService;
  friend class RssHttpFetch;
  friend class SavedSubscriptionResultSnapshot;
  enum class Purpose {Manual,Automatic};
  static std::expected<SavedSubscriptionSourceSnapshot,SavedRssSourceError> readAutomatic(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,SavedSubscriptionSelector);
  static std::expected<SavedSubscriptionSourceSnapshot,SavedRssSourceError> readImpl(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,SavedSubscriptionSelector,Purpose);
  const detail::SubscriptionAutomaticPolicyValues* automaticPolicy()const noexcept;
  struct Impl;
  explicit SavedSubscriptionSourceSnapshot(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
  const SubscriptionSavedRow& metadata()const noexcept;
  const SubscriptionResults& results()const noexcept;
  const core::Node& selectedSourceRow()const noexcept;
  std::string_view selectedSourceBytes()const noexcept;
  std::expected<std::string,SavedRssSourceError> freezeAcquisitionSource(const storage::ProfileLease&,std::uint64_t)const;
  const SubscriptionNumericFilter* numericFilter()const noexcept;
  const SubscriptionLiteralFilter* literalFilter()const noexcept;
  static std::expected<RssContext,SavedRssSourceError> resultUrlContext(std::string_view);
  std::expected<void,SavedRssSourceError> resultHeaderPolicy()const;
  std::expected<std::optional<QList<QSslCertificate>>,SavedRssSourceError> resultTrust(
      const storage::ProfileLease&,std::uint64_t)const;
  std::expected<SubscriptionHistoryCommit,SavedRssSourceError> prepareCheck(const RssDocument&,std::int64_t)const;
  std::expected<SubscriptionHistoryCommit,SavedRssSourceError> prepareState(const std::vector<std::string>&,bool,bool,std::int64_t)const;
  std::expected<void,SavedRssSourceError> publish(
      const storage::ProfileLease&,std::uint64_t,const SubscriptionHistoryCommit&);
};
} // namespace biglybtc::search
