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
struct DefinitionChange {
 enum class Operation { Added, Removed };
 Operation operation;
 std::u16string_view name;
};
// One native logical startup's category identities. Initial special IDs1/2 and
// declaration-order user IDs match original construction; saved sourceIndex is
// never treated as a stable UID. Removal/reordering cannot recycle an identity.
class Roster final {
public:
 Roster();
 // Only supply a change when the normal category owner just committed it.
 std::expected<void,runtime::Error> observe(std::span<const runtime::Text> userNames,
     std::optional<DefinitionChange> change = {});
 const std::vector<Entry>& entries()const noexcept{return entries_;}
 // Known enumeration of the original HashMap with linked buckets.
 const std::optional<std::vector<std::size_t>>& enumeration() const noexcept { return order_; }
private:
 std::vector<Entry> entries_;
 std::optional<std::vector<std::size_t>> order_;
 std::size_t capacity_ = 16;
 bool observed_ = false;
 std::int64_t next_=3;
};
}
