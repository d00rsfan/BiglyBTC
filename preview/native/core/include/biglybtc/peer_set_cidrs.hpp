// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_registry.hpp"
#include <cstdint>
#include <utility>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::core::tags::peer_sets {
enum class Error {Invalid,Unsupported,Bounds,Memory};
struct Range {std::uint32_t first=0,last=0;bool operator==(const Range&)const=default;};
struct Limits {std::size_t ranges=256,tokenBytes=64;};
// Owning data only. No DNS,peer/profile access,tag/cache grant or rate action.
// This subset represents nonempty IPv4 CIDRs without other selector clauses.
class Ipv4CidrSet final {
public:
 static std::expected<Ipv4CidrSet,Error> create(std::span<const std::string>,bool inverse=false,Limits={});
 // Supply a captured numeric IPv4 in the Public network. Unspecified/unknown
 // and other-family/network sources stay Unknown, including inverse mode.
 runtime::Truth match(std::optional<std::uint32_t> publicIpv4)const noexcept;
 std::span<const Range> ranges()const noexcept{return ranges_;}
private:
 Ipv4CidrSet(std::vector<Range> ranges,bool inverse):ranges_(std::move(ranges)),inverse_(inverse){}
 std::vector<Range> ranges_;bool inverse_=false;
};
}
