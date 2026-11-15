// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include "policy.hpp"
#include "../msgsync/bloom.hpp"
#include <expected>
#include <functional>
#include <map>
#include <vector>
namespace biglybtc::services::azureus_key_block {
enum class StateError {Input,Limit,Crypto,Resources};
using Verifier=std::function<Verification(std::string_view,std::string_view)>;
inline constexpr std::size_t maximumRecords=8192,maximumRecordBytes=4*1024*1024;
struct Admission {bool returnedBlocked=false,changed=false;};
class State final {
public:
 // Constructor injection is an internal protocol dependency, not saved policy.
 explicit State(Verifier verifier=verifyOfficialStatus):verify_(std::move(verifier)){}
 std::expected<Admission,StateError> admit(std::string request,std::string signature,std::optional<std::string_view> directOrigin,std::int64_t wallMilliseconds);
 std::expected<void,StateError> restore(std::span<const Record>);
 const Record* find(std::string_view key)const;
 const Record* blocked(std::string_view key)const;
 std::vector<Record> records(bool directOnly=false)const;
 bool hasSent(std::string_view key,std::string_view contactId)const;
 bool markSent(std::string_view key,std::string_view contactId);
 bool purgeDirect();
 std::uint64_t revision()const noexcept{return revision_;}
private:
 struct Entry {Record record;std::optional<msgsync::AddOnlyBloom> sent;};
 Verifier verify_;std::map<std::string,Entry,std::less<>> records_;std::size_t bytes_=0;std::uint64_t revision_=0;
 std::optional<msgsync::AddOnlyBloom> failed_;std::int64_t failedAt_=0;
};
struct Saved {std::vector<Record> records;std::size_t rejected=0;};
// Pure worker-side codecs; they never open files or confer network authority.
std::expected<Saved,StateError> decodeSaved(const core::Node&,std::int64_t wallMilliseconds,Verifier=verifyOfficialStatus);
std::expected<std::string,StateError> encodeSaved(const core::Node& original,std::span<const Record>);
}
