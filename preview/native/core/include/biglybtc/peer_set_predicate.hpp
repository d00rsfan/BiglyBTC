// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "peer_set_cidrs.hpp"
namespace biglybtc::core::tags::peer_sets {
enum class Network {Unknown,Public,I2p,Tor};
enum class Family {Unknown,Ipv4,Ipv6};
struct Endpoint {Network network=Network::Unknown;Family family=Family::Unknown;std::optional<std::uint32_t> ipv4;};
// Owning selector data. Supplied endpoint facts create no provider/effect grant.
// Inverse is applied independently to CIDR and network dimensions,as in Java.
class Predicate final {
public:
 static std::expected<Predicate,Error> create(std::span<const std::string>,bool inverse=false,Limits={});
 runtime::Truth match(const Endpoint&)const noexcept;
 runtime::Truth match(std::optional<std::uint32_t> address)const noexcept;
 std::span<const Range> ranges()const noexcept{return ranges_?ranges_->ranges():std::span<const Range>{};}
private:
 Predicate()=default;
 std::optional<Ipv4CidrSet> ranges_;unsigned networks_=0;bool inverse_=false;
};
}
