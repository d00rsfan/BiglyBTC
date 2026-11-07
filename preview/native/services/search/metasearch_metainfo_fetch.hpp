// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_http_fetch.hpp"
#include "saved_metasearch_result.hpp"
namespace biglybtc::search {
struct MetasearchMetainfoCompletion {
  std::uint64_t session=0,nonce=0;
  QUrl initialUrl,finalUrl;
  QByteArray bytes; // Uninspected binary bytes; never intake/read authority.
  std::size_t gets=0,responseOctets=0;
};
class MetasearchMetainfoFetch final {
public:
  static std::expected<std::unique_ptr<MetasearchMetainfoFetch>,RssFetchError> openOneShot(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      const SavedMetasearchResultHandle&,std::stop_token receiverStop={});
  ~MetasearchMetainfoFetch();
  MetasearchMetainfoFetch(const MetasearchMetainfoFetch&)=delete;
  std::expected<std::optional<MetasearchMetainfoCompletion>,RssFetchError> poll(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession);
  RssFetchObservation observation()const;
  void requestCancel()noexcept;
  void close()noexcept;
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  static std::expected<std::unique_ptr<MetasearchMetainfoFetch>,RssFetchError> openFixture(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      const SavedMetasearchResultHandle&,RssHttpFetch::PrivateEndpointFixture,
      std::stop_token receiverStop={});
#endif
private:
  friend class SavedMetasearchService;
  friend class ::biglybtc::app::SessionBridge;
  bool matchesHandle(const storage::ProfileLease&,const std::filesystem::path&,
      std::uint64_t,const SavedMetasearchResultHandle&,std::stop_token)const noexcept;
  explicit MetasearchMetainfoFetch(std::unique_ptr<RssHttpFetch::Impl>);
  std::unique_ptr<RssHttpFetch::Impl> impl_;
};
} // namespace biglybtc::search
