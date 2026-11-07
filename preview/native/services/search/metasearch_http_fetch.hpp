// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_http_fetch.hpp"
#include "saved_metasearch_source.hpp"
#include <string_view>
namespace biglybtc::search {
struct MetasearchFetchCompletion {
  std::uint64_t session=0,nonce=0;
  QUrl initialUrl,finalUrl;
  RssContext context;
  QByteArray bytes; // Owning bounded UTF8 document; parse on source worker.
  std::size_t gets=0,responseOctets=0;
};
class MetasearchHttpFetch final {
public:
  static std::expected<std::unique_ptr<MetasearchHttpFetch>,RssFetchError> openOneShot(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedMetasearchSelector,std::string queryUtf8,std::stop_token receiverStop={});
  ~MetasearchHttpFetch();
  MetasearchHttpFetch(const MetasearchHttpFetch&)=delete;
  std::expected<std::optional<MetasearchFetchCompletion>,RssFetchError> poll(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession);
  RssFetchObservation observation()const;
  void requestCancel()noexcept;
  void close()noexcept;
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  static std::expected<std::unique_ptr<MetasearchHttpFetch>,RssFetchError> openFixture(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedMetasearchSelector,std::string queryUtf8,RssHttpFetch::PrivateEndpointFixture,
      std::stop_token receiverStop={});
#endif
private:
  friend class SavedMetasearchService;
  friend class ::biglybtc::app::SessionBridge;
  bool matchesQuery(const storage::ProfileLease&,const std::filesystem::path&,
      std::uint64_t,const SavedMetasearchSelector&,std::string_view queryUtf8,
      std::stop_token)const noexcept;
  explicit MetasearchHttpFetch(std::unique_ptr<RssHttpFetch::Impl>);
  std::unique_ptr<RssHttpFetch::Impl> impl_;
};
} // namespace biglybtc::search
