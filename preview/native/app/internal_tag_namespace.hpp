// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "tag_manager_startup.hpp"
#include <biglybtc/internal_tag_owner.hpp>
#include <biglybtc/tag_constraints.hpp>
namespace biglybtc::app {
class InternalTagNamespace;
struct InternalTagNamespaceState;
class InternalTagSnapshot final {
public:
 InternalTagSnapshot()=default;
 bool live(std::uint64_t session)const noexcept;
 bool current(const storage::ProfileLease&,std::uint64_t session)const noexcept;
 core::tags::constraints::Result<core::tags::runtime::Type> project(std::uint64_t session)const;
 bool sameRevision(const InternalTagSnapshot&)const noexcept;
 core::tags::constraints::Result<core::tags::runtime::Type> project(
   const storage::ProfileLease&,std::uint64_t session)const;
private:
 struct Data;std::shared_ptr<const Data> data_;
 explicit InternalTagSnapshot(std::shared_ptr<const Data> data):data_(std::move(data)){}
 friend class InternalTagNamespace;
};
// Profile-bound metadata source; no network,payload,tag.config or effect writer.
// Startup mode is sealed by its actual retained lease; offline/disabled owns no
// active Core namespace. Only this adapter mints current namespace snapshots.
class InternalTagNamespace final {
public:
 using Token=core::tags::internal::Token;
 static std::expected<std::unique_ptr<InternalTagNamespace>,storage::Error> activate(
   const storage::ProfileLease&,std::uint64_t session,bool normal,
   const storage::TagManagerStartupMode&,core::tags::internal::Limits={});
 std::expected<std::optional<InternalTagSnapshot>,storage::Error> snapshot()const;
 std::expected<Token,storage::Error> createTag(std::u16string_view,bool autoAdd);
 std::expected<void,storage::Error> attach(const Token&);
 std::expected<void,storage::Error> rename(const Token&,std::u16string_view);
 std::expected<void,storage::Error> remove(const Token&);
 std::expected<void,storage::Error> close();
private:
 explicit InternalTagNamespace(std::shared_ptr<InternalTagNamespaceState> state):state_(std::move(state)){}
 std::shared_ptr<InternalTagNamespaceState> state_;
};
}
