// SPDX-License-Identifier: MPL-2.0
// Derived from paired mlDHT067 BloomFilterBEP33/ScrapeResponseHandler.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
namespace biglybtc::services::mainline_scrape {
enum class Error { Invalid,Limit,Crypto,Memory,AmbiguousDialect,Closed,Cancelled };
struct Address {
  std::array<std::uint8_t,16> bytes{};std::uint8_t length=4;
  bool operator==(const Address&)const=default;
};
std::expected<Address,Error> address(std::span<const std::uint8_t>);
bool permittedDirect(const Address&,std::uint16_t port)noexcept;
class Bloom final {
public:
  static constexpr std::size_t Bytes=256,Bits=2048;
  static std::expected<Bloom,Error> parse(std::string_view);
  std::expected<void,Error> insert(const Address&)noexcept;
  void merge(const Bloom&)noexcept;
  std::int32_t estimate()const noexcept;
  const std::array<std::uint8_t,Bytes>& bytes()const noexcept{return bytes_;}
private:std::array<std::uint8_t,Bytes> bytes_{};
};
enum class Dialect { OriginalBFse,LibtorrentBFsd };
struct Response {
  std::optional<Bloom> seeds,peers;
  std::vector<Address> direct; // Original nonbogon addresses, deduplicated by aggregate.
  // MessageDecoder retains6/18-byte items even when AddressUtils later calls them bogons.
  std::size_t decodedPeerItems=0; // PeerLookupTask gates collection on this count, not direct.size().
};
inline constexpr std::size_t MaximumPeers=128,MaximumResponses=64;
// Only the authenticated get_peers response BODY. Caller owns transaction,
// source endpoint/node ID and admission/generation correlation. No network IO.
// Duplicate dictionary keys and mixed/wrong seed dialects fail closed.
std::expected<Response,Error> decode(std::string_view,Dialect=Dialect::OriginalBFse);
struct Summary {std::int32_t seeds=0,peers=0;std::size_t direct=0,responses=0;bool operator==(const Summary&)const=default;};
// One bounded round. Duplicate responses union idempotently. finish/cancel are
// explicit terminal transitions; no expiry, scheduler, target authority or IO.
class Aggregate final {
public:
  std::expected<void,Error> add(const Response&);
  std::expected<Summary,Error> finish()noexcept;
  void cancel()noexcept;
private:
  Bloom seeds_,peers_;std::vector<Address> direct_;std::size_t responses_=0;
  bool closed_=false,cancelled_=false;
};
}
