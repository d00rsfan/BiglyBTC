// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_registry.hpp"
#include <thread>
namespace biglybtc::core::tags::internal {
enum class Error {Closed,WrongThread,Foreign,Removed,Registered,Bounds,Memory};
struct Limits {std::size_t objects=4096,registered=4096,nameUnits=4096,totalUnits=262144,creations=65536;};
class Owner;
class Token final {
public:
 Token()=default;
 std::optional<std::int32_t> id()const noexcept;
private:
 struct Data;struct Life;
 Token(std::shared_ptr<Data> data,std::weak_ptr<const Life> life):data_(std::move(data)),life_(std::move(life)){}
 std::shared_ptr<Data> data_;std::weak_ptr<const Life> life_;
 friend class Owner;
};
struct Entry {std::int32_t id=0;runtime::Text rawName;};
struct Snapshot {
 std::uint64_t revision=0;std::vector<Entry> entries;
 static constexpr std::int32_t type=5,taggableType=2;
 static constexpr bool persistent=false,automatic=false,properties=true;
};
// Exclusive in-memory metadata lifecycle only. Detached tokens do not enter the
// registered roster. Snapshots grant no membership,census,profile or effect rights.
class Owner final {
public:
 static std::expected<std::unique_ptr<Owner>,Error> create(Limits={});
 ~Owner();
 Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
 std::expected<Token,Error> createTag(std::u16string_view,bool autoAdd);
 std::expected<void,Error> attach(const Token&);
 std::expected<void,Error> rename(const Token&,std::u16string_view);
 std::expected<void,Error> remove(const Token&);
 std::expected<Snapshot,Error> snapshot()const;
 std::expected<void,Error> close()noexcept;
private:
 explicit Owner(Limits);
 std::optional<Error> check()const noexcept;
 std::optional<Error> check(const Token&)const noexcept;
 std::size_t liveUnits()const noexcept;
 Limits limits_;std::thread::id thread_;std::shared_ptr<const Token::Life> life_;
 std::vector<std::shared_ptr<Token::Data>> registered_;
 std::vector<std::weak_ptr<Token::Data>> allocated_;
 std::uint32_t next_=0;std::uint64_t revision_=1;std::size_t creations_=0;bool closed_=false;
};
}
