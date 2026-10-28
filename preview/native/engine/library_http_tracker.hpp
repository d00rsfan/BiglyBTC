#pragma once
#include "library_udp_tracker.hpp"
#include <QHostAddress>
#include <QSslConfiguration>
#include <QUrl>
#include <functional>
#include <memory>

namespace biglybtc::engine::detail {
struct LibraryHttpRoute {
  QUrl scrape;
  std::vector<QHostAddress> locals;
  bool ipv6=false;
  bool operator==(const LibraryHttpRoute&)const=default;
};
std::expected<LibraryHttpRoute,storage::Error> libraryHttpRoute(const NetworkPolicy&,std::string_view);
std::string libraryHttpRouteScope(const LibraryHttpRoute&,std::string_view);
// A DNS answer is only a proposal. Caller must authenticate current saved
// permissions before this function's chosen numeric route can be connected.
std::optional<std::pair<QHostAddress,QHostAddress>> libraryHttpEndpoint(
    const LibraryHttpRoute&,const QList<QHostAddress>&);
namespace http_tracker_wire {
struct Body {bool complete=false,unsupported=false;QByteArray bytes;bool failed=false;};
Body response(const QByteArray&,bool eof);
core::QueueTrackerTerminal scrape(std::string_view,std::span<const std::byte,20>,
    std::int64_t now,std::int64_t wall,std::int64_t started,bool singleOnly,unsigned fuzzSeconds);
std::int64_t interval(std::int32_t minimum,std::int32_t seeds,unsigned fuzzSeconds) noexcept;
}
// One worker-owned request and one reusable private event-loop thread. Neither
// the network thread nor DNS callbacks own a ProfileLease or response object.
class LibraryHttpTracker final {
public:
  using Request=LibraryUdpTracker::Request;
  using Result=LibraryUdpTracker::Result;
  LibraryHttpTracker();
  ~LibraryHttpTracker();
  LibraryHttpTracker(const LibraryHttpTracker&)=delete;
  LibraryHttpTracker& operator=(const LibraryHttpTracker&)=delete;
  const Request* active()const noexcept;
  bool resolvedProposalPending() const; // Passive internal diagnostic, no DNS/pump/authority.
  std::expected<void,storage::Error> start(Request,const LibraryHttpRoute&,std::int64_t now,
      std::int64_t wall,bool singleOnly=false);
  std::expected<std::optional<Result>,storage::Error> poll(std::int64_t now,std::int64_t wall,
      const std::function<bool(const Request&)>& authorise);
  void cancel() noexcept;
  void close() noexcept;
  // Internal transport test seam: caller-created trust anchors, never persisted
  // settings or a route fallback. PersistentSession never calls this method.
  bool testTlsConfiguration(const QSslConfiguration&);
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::engine::detail
