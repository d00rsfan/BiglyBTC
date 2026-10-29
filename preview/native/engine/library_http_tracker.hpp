#pragma once
#include "library_udp_tracker.hpp"
#include <QHostAddress>
#include <QHostInfo>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QUrl>
#include <functional>
#include <memory>

namespace biglybtc::engine::detail {
struct LibraryHttpSocks5 {
  QString host;
  quint16 port=0;
  QByteArray username,password; // SOCKS credentials only; never HTTP Basic.
  bool remoteDns=true;
  bool operator==(const LibraryHttpSocks5&)const=default;
};
struct LibraryHttpRoute {
  QUrl scrape;
  std::vector<QHostAddress> locals;
  bool ipv6=false;
  std::optional<LibraryHttpSocks5> socks5=std::nullopt;
  bool operator==(const LibraryHttpRoute&)const=default;
};
std::expected<LibraryHttpRoute,storage::Error> libraryHttpRoute(const NetworkPolicy&,std::string_view);
std::expected<LibraryHttpRoute,storage::Error> libraryHttpRedirectRoute(
    const LibraryHttpRoute&,const QByteArray& location,std::span<const std::byte,20> hash);
std::string libraryHttpRouteScope(const LibraryHttpRoute&,std::string_view);
// A DNS answer is only a proposal. Caller must authenticate current saved
// permissions before this function's chosen numeric route can be connected.
std::optional<std::pair<QHostAddress,QHostAddress>> libraryHttpEndpoint(
    const LibraryHttpRoute&,const QList<QHostAddress>&);
std::optional<std::pair<QHostAddress,QHostAddress>> libraryHttpProxyEndpoint(
    const LibraryHttpRoute&,const QList<QHostAddress>&);
// Empty address means an authorised remote-DNS name; nullopt is refusal.
std::optional<QHostAddress> libraryHttpProxyTarget(const LibraryHttpRoute&,const QList<QHostAddress>&,bool trackerHostNotFound=false);
namespace http_tracker_wire {
struct Body {bool complete=false,unsupported=false;QByteArray bytes;bool failed=false;QByteArray location={};
  QByteArray basicRealm={};bool basicAuthentication=false;};
Body response(const QByteArray&,bool eof);
core::QueueTrackerTerminal scrape(std::string_view,std::span<const std::byte,20>,
    std::int64_t now,std::int64_t wall,std::int64_t started,bool singleOnly,unsigned fuzzSeconds);
std::int64_t interval(std::int32_t minimum,std::int32_t seeds,unsigned fuzzSeconds) noexcept;
}
// Internal asynchronous resolver boundary. Completion must be delivered on the
// supplied context's thread and is revoked when that context is destroyed.
// Empty functions select QHostInfo; injected implementations must supply both.
struct LibraryHttpResolver {
  using Completion=std::function<void(const QHostInfo&)>;
  std::function<int(const QString&,QObject*,Completion)> submit;
  std::function<void(int)> cancel;
};
// One worker-owned request and one reusable private event-loop thread. Neither
// the network thread nor DNS callbacks own a ProfileLease or response object.
class LibraryHttpTracker final {
public:
  using Request=LibraryUdpTracker::Request;
  using Result=LibraryUdpTracker::Result;
  using Authentication=std::function<std::expected<std::optional<QByteArray>,storage::Error>(
      const Request&,const LibraryHttpRoute&,const QByteArray& realm)>;
  explicit LibraryHttpTracker(LibraryHttpResolver resolver={});
  ~LibraryHttpTracker();
  LibraryHttpTracker(const LibraryHttpTracker&)=delete;
  LibraryHttpTracker& operator=(const LibraryHttpTracker&)=delete;
  const Request* active()const noexcept;
  std::vector<std::string> resolverSubmissions() const; // Passive configured-resolver submissions; not DNS-wire completion/join.
  bool authenticationProposalPending() const; // Passive; never reads credentials or approves.
  bool redirectProposalPending() const; // Passive; never resolves/approves.
  bool resolvedProposalPending() const; // Passive internal diagnostic, no DNS/pump/authority.
  std::expected<void,storage::Error> start(Request,const LibraryHttpRoute&,std::int64_t now,
      std::int64_t wall,bool singleOnly=false,
      const std::optional<QList<QSslCertificate>>& trustAnchors=std::nullopt);
  std::expected<std::optional<Result>,storage::Error> poll(std::int64_t now,std::int64_t wall,
      const std::function<bool(const Request&)>& authorise,const Authentication& authenticate={});
  void cancel() noexcept;
  void close() noexcept;
  // Internal transport test seam: caller-created trust anchors, never persisted
  // settings or a route fallback. PersistentSession never calls this method.
  bool testTlsConfiguration(const QSslConfiguration&);
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::engine::detail
