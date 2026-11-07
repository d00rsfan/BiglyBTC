// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_http_fetch.hpp"
#include <QByteArray>
#include <string>
#include <string_view>

namespace biglybtc::app {class SavedSubscriptionAcquisition;}
namespace biglybtc::search {
// Raw bounded response bytes, never validated torrent geometry/intake permission.
// The acquisition owner must inspect exact full identities before any import.
struct SubscriptionMetainfoCompletion {
  std::uint64_t session=0,nonce=0;
  QUrl initialUrl,finalUrl;
  QByteArray bytes;
  std::size_t gets=0,responseOctets=0;
};
class SubscriptionMetainfoFetch final {
public:
  // Reads actual saved source/result and its effective Original download link.
  // No caller URL/description/permission callback is accepted.
  static std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> openOneShot(
      const storage::ProfileLease&,const std::filesystem::path& canonicalProfile,
      std::uint64_t session,SavedSubscriptionSelector,std::string exactResultKey,
      std::stop_token receiverStop={});
  static std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> openAutomaticOneShot(
      const storage::ProfileLease&,const std::filesystem::path& canonicalProfile,
      std::uint64_t session,SavedSubscriptionSelector,std::string exactResultKey,
      std::stop_token receiverStop={});
  ~SubscriptionMetainfoFetch();
  SubscriptionMetainfoFetch(const SubscriptionMetainfoFetch&)=delete;
  std::expected<std::optional<SubscriptionMetainfoCompletion>,RssFetchError> poll(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession);
  RssFetchObservation observation()const;
  void requestCancel()noexcept;
  void close()noexcept;

#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  static std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> openFixture(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedSubscriptionSelector,std::string exactResultKey,RssHttpFetch::PrivateEndpointFixture,
      std::stop_token receiverStop={});
  static std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> openAutomaticFixture(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedSubscriptionSelector,std::string exactResultKey,RssHttpFetch::PrivateEndpointFixture,
      std::stop_token receiverStop={});
#endif
private:
  friend class ::biglybtc::app::SavedSubscriptionAcquisition;
  // Identity-only predicate for a real returned owner, never phase permission.
  bool matchesSubscriptionResultSource(const storage::ProfileLease&,const std::filesystem::path&,
      std::uint64_t,const SavedSubscriptionSelector&,std::string_view exactResultKey,
      std::stop_token)const noexcept;
  bool matchesAutomaticSubscriptionResultSource(const storage::ProfileLease&,const std::filesystem::path&,
      std::uint64_t,const SavedSubscriptionSelector&,std::string_view exactResultKey,
      std::stop_token)const noexcept;
  explicit SubscriptionMetainfoFetch(std::unique_ptr<RssHttpFetch::Impl>);
  std::unique_ptr<RssHttpFetch::Impl> impl_;
};
} // namespace biglybtc::search
