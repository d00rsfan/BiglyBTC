// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <unordered_set>
namespace biglybtc::services::azureus_db_runtime::survey {
using MappingId=std::uint64_t;
enum class StateError {Input,Capacity,Memory};
// Metadata only. A mapping identity belongs to one DB session and grants no
// profile, endpoint, nonce or value authority. Empty live mappings retain IDs.
class ContactState final {
public:
 static std::expected<ContactState,StateError> create(std::int32_t instance,std::int64_t now,std::int64_t lifetimeJitter,std::size_t maximumKnown=8192);
 std::expected<bool,StateError> add(MappingId);
 void remove(MappingId id)noexcept{known_.erase(id);}
 // Explicit cache-pressure eviction; other original state fields are retained.
 void clearKnown()noexcept{known_.clear();}
 bool contains(MappingId id)const noexcept{return known_.contains(id);}
 const std::unordered_set<MappingId>& known()const noexcept{return known_;}
 void updateInstance(std::int32_t)noexcept;
 void updateUseTime(std::int64_t now)noexcept{lastUse_=now;}
 void success()noexcept{failures_=0;}
 void failure()noexcept;
 bool expired(std::int64_t now)const noexcept;
 std::int64_t created()const noexcept{return created_;}
 std::int64_t deadline()const noexcept{return deadline_;}
 std::int64_t lastUse()const noexcept{return lastUse_;}
 std::int32_t instance()const noexcept{return instance_;}
 std::int32_t failures()const noexcept{return failures_;}
private:
 ContactState(std::int32_t,std::int64_t,std::int64_t,std::size_t);
 std::int32_t instance_=0,failures_=0;std::int64_t created_=0,deadline_=0,lastUse_=0;std::size_t maximum_=8192;
 std::unordered_set<MappingId> known_;
};
}
