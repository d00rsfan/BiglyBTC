// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::core::tags::tracker {
enum class Error {Unsupported,Bounds,Memory};
template<class T>using Result=std::expected<T,Error>;
struct Endpoint {
  std::string host;std::int32_t port=-1,defaultPort=-1;
  bool operator==(const Endpoint&)const=default;
};
struct Limits {std::size_t endpoints=64,properties=64,textBytes=2048,hosts=4096,totalBytes=262144;};
// Qualified ASCII http/https/udp/wss URL subset only. Inert lexical metadata,
// no DNS, tracker request, local/public address or endpoint authority.
Result<Endpoint> endpoint(std::string_view url,Limits={});
struct Origin {
  std::string name,selector;
  bool operator==(const Origin&)const=default;
};
struct SelectionUrl {
  Endpoint endpoint;
  std::optional<Origin> origin;
};
// The original All Trackers view excludes .dht placeholders from its table.
// The tracker property still reads their original endpoint values.
Result<SelectionUrl> selectionUrl(std::string_view url,Limits={});
// Exact original getUniqueTrackerHosts(include_port=true) and suffix expansion.
// Unknown Unicode case/URL forms refuse; never truncate into a negative match.
Result<std::vector<std::string>> augmentedHosts(std::span<const Endpoint>,Limits={});
Result<bool> matches(std::span<const Endpoint>,std::span<const std::string> property,Limits={});
enum class Event {Startup,DownloadCreated,PropertyChanged,AnnounceSync};
enum class Membership {Keep,Add,Remove};
// Source characterisation only: the actual owner must still bind source,
// execute real effects and revalidate before CAS. Startup does not reconcile.
Membership project(Event,bool propertyHasEntries,bool member,bool match)noexcept;
}
