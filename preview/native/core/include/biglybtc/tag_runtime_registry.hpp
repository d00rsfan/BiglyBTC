// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::core::tags::runtime {
using Text=std::u16string;
enum class Membership {Unknown,Absent,Present};
enum class Truth {Unknown,No,Yes};
enum class Error {Invalid,Bounds,Memory};
// Data for ONE observed subject, never profile/provider/effect authority.
struct Tag {
 std::int32_t id=0;std::optional<std::int32_t> taggableTypes;
 std::optional<Text> name,group;bool groupKnown=true;
 Membership membership=Membership::Unknown;
};
struct Type {
 std::int32_t id=0;
 std::vector<Tag> tags;
 bool complete=false;
 bool nameOrderKnown=false;
};
struct Limits {std::size_t types=64,tags=4096,nameUnits=4096,totalUnits=262144;};
struct Reference {
 std::size_t typeIndex=0,tagIndex=0;std::int32_t type=0,id=0;
 std::int64_t uid()const noexcept;
 bool operator==(const Reference&)const=default;
};
struct Selection {std::vector<Reference> tags;bool complete=false;};
// Immutable owning metadata; preserves supplied original registration order.
// Unknown names/members and unobserved types are explicit. References identify
// data in this snapshot and grant no live tag/provider or mutation rights.
class Registry final {
public:
 Registry()=default;
 static std::expected<Registry,Error> create(std::vector<Type>,bool completeTypes=false,Limits={});
 std::expected<Selection,Error> lookupName(std::u16string_view)const;
 // Original property-capable families1,2,3,5 and special peer family4.
 // Requires their complete observed name rosters; featureless6/7 do not mask
 // a unique dependency. Aliases require known within-type lookup order.
 // This does not make the general runtime census or memberships complete.
 std::expected<Selection,Error> lookupConstraintName(std::u16string_view)const;
 std::expected<Selection,Error> tagged(std::int32_t taggableType)const;
 Truth hasName(std::u16string_view,std::int32_t taggableType)const noexcept;
 // Original known family domain only; all actual direct-download name
 // sources1..5 must be present/complete. Peer names require an explicitly
 // complete current cache observation; featureless UI6/TT_NONE7 are separate.
 // This proves no global registry/source completeness or effect permission.
 Truth downloadConstraintName(std::u16string_view,bool peerObservationComplete)const noexcept;
 Truth downloadConstraintGroup(std::u16string_view)const noexcept;
 Truth hasGroup(std::u16string_view,std::int32_t taggableType)const noexcept;
 const Tag* at(const Reference&)const noexcept;
 std::size_t tagCount()const noexcept;
 std::size_t textUnits()const noexcept;
private:
 struct Data;explicit Registry(std::shared_ptr<const Data>);
 std::shared_ptr<const Data> data_;
};
}
