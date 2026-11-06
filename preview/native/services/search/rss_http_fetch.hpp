// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_rss_source.hpp"
#include <QUrl>
#include <optional>
#include <stop_token>

#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
#include <QHostAddress>
#include <QHostInfo>
#include <QObject>
#include <functional>
#endif

namespace biglybtc::search {
enum class RssFetchError {Source,WrongWorker,Busy,UnsupportedRoute,Limit,Dns,Tls,Http,
  Encoding,Parse,Cancelled,Deadline,Closed,Memory};
enum class RssFetchPhase {BeforeDNS,Resolving,BeforeConnect,Connecting,BeforeGET,
  Reading,BeforeDelivery,Complete,Failed,Cancelled,Closed};
struct RssFetchObservation {
  RssFetchPhase phase=RssFetchPhase::BeforeDNS;
  std::uint64_t nonce=0,hop=1,sourceChecks=0;
  std::size_t gets=0,responseOctets=0,resolverSubmissions=0;
  bool joined=false;
  std::optional<RssFetchError> failure;
};
struct RssFetchCompletion {
  std::uint64_t session=0,nonce=0;
  QUrl initialUrl,finalUrl;
  RssContext context;
  RssDocument document;
  std::size_t gets=0,responseOctets=0;
};

// Concrete one-shot provider for the existing serialized profile worker.
// Factory/read/current/poll stay on that worker. The private socket event loop
// never reads the profile or accepts a caller-supplied authority callback/DTO.
class RssHttpFetch final {
public:
  static std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> openOneShot(
      const storage::ProfileLease&,const std::filesystem::path& profileDirectory,
      std::uint64_t profileSession,SavedRssSelector,std::stop_token receiverStop={});
  ~RssHttpFetch();
  RssHttpFetch(const RssHttpFetch&)=delete;
  std::expected<std::optional<RssFetchCompletion>,RssFetchError> poll(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession);
  RssFetchObservation observation()const;
  // Atomic receiver cancellation is safe from another thread. close/destruction
  // is external lifetime cleanup after owner calls stop; it joins the owned
  // event loop, not Qt/global resolver threads. No mid-parser interruption.
  void requestCancel()noexcept;
  void close()noexcept;

#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  // Exists only in a separately compiled fixture image. The real saved source
  // is still mandatory. Only exact loopback endpoint substitution is possible;
  // production has neither this type/factory nor an authority/test callback.
  struct PrivateEndpointFixture {
    QHostAddress endpoint;
    using Completion=std::function<void(const QHostInfo&)>;
    std::function<int(const QString&,QObject*,Completion)> resolve;
    std::function<void(int)> cancelResolve;
  };
  static std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> openFixture(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedRssSelector,PrivateEndpointFixture,std::stop_token receiverStop={});
#endif
private:
  struct Impl;
  explicit RssHttpFetch(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
