// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_runtime_registry.hpp>
#include <span>
namespace biglybtc::core::tags::category {
enum class Kind {User,All,Uncategorized};
// Original membership boundary: USER consults its actual member set; ALL and
// UNCAT inspect the supplied download/category facts. Never effect authority.
bool member(Kind,bool isDownload,bool userSetMember,std::optional<Kind> assigned)noexcept;
struct Entry {std::int32_t id;runtime::Text rawName;Kind kind;};
// One native logical startup's category identities. Initial special IDs1/2 and
// declaration-order user IDs match original construction; saved sourceIndex is
// never treated as a stable UID. Removal/reordering cannot recycle an identity.
class Roster final {
public:
 Roster();
 std::expected<void,runtime::Error> observe(std::span<const runtime::Text> userNames);
 const std::vector<Entry>& entries()const noexcept{return entries_;}
private:
 std::vector<Entry> entries_;
 std::int64_t next_=3;
};
}
