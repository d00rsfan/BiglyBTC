// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bencode.hpp"
#include "peer_set_predicate.hpp"
namespace biglybtc::core::tags::peer_sets {
struct SavedLimits {std::size_t lines=4096,lineBytes=4096,totalBytes=1048576,sets=256,nameBytes=1024,ranges=256;};
struct SavedSet {
 std::string name;std::optional<std::int32_t> id;std::optional<std::string> group;
 std::optional<Predicate> predicate;
};
// Read-only,owning declarations. Missing identities/unsupported selectors stay
// unknown. This does not register tags,allocate IDs,poll peers or grant effects.
class SavedPeerSets final {
public:
 static std::expected<SavedPeerSets,Error> read(const Document&,SavedLimits={});
 std::span<const SavedSet> sets()const noexcept{return sets_;}
 bool complete()const noexcept{return complete_;}
 bool enabled()const noexcept{return enabled_;}
private:
 std::vector<SavedSet> sets_;bool complete_=true,enabled_=true;
};
}
