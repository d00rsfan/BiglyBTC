// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::core {
struct FileMergeSettings {
 bool enabled=true,extended=false;std::int64_t tolerance=0,minPieces=5;
 bool operator==(const FileMergeSettings&)const=default;
};
struct FileMergeError {enum Code {Options,Geometry,Memory} code;std::string field;};
std::expected<FileMergeSettings,FileMergeError> readFileMergeSettings(const Document*);
struct MergeFile {
 std::string owner,name;std::uint64_t admission=0;int index=0;
 std::int64_t offset=0,bytes=0,pieceLength=0;
 bool ownerComplete=false,skipped=false,padding=false,ownerEligible=true;
 std::optional<std::array<std::byte,32>> root;
};
struct FileMergePair {std::size_t first=0,second=0;bool sameRoot=false;};
struct FileMergePlan {std::vector<FileMergePair> pairs;bool truncated=false;std::size_t eligibleFiles=0;};
struct FileMergeLimits {std::size_t files=200000,pairs=4096,groupMembers=64,nameBytes=16*1024*1024;};
// Similarity selects candidates only. Every assembled target piece still needs
// cryptographic validation and fresh retained-owner/write-policy checks.
std::expected<FileMergePlan,FileMergeError> planFileMerges(std::span<const MergeFile>,FileMergeSettings,FileMergeLimits={});

struct SwarmMergeSearchFile {
 int index=0;
 std::int64_t bytes=0,completed=0;
 bool selected=false;
};

// Per-admission policy for the original Ratings alternative-file lookup.
class SwarmMergeSearch final {
public:
 using Clock=std::chrono::steady_clock;
 static constexpr auto SamplePeriod=std::chrono::minutes(2);
 static constexpr std::int64_t SlowRate=10*1024,MinimumFileBytes=75*1024*1024;

 bool due(Clock::time_point now)const noexcept;
 void suspend()noexcept;
 bool observe(Clock::time_point now,std::int64_t received)noexcept;
 std::optional<int> select(Clock::time_point now,std::span<const SwarmMergeSearchFile>)noexcept;

private:
 static constexpr std::size_t Samples=8;
 std::optional<Clock::time_point> last_,retry_;
 std::int64_t received_=0;
 std::array<std::int64_t,Samples> deltas_{};
 std::size_t count_=0,cursor_=0,examined_=0;
 std::array<int,3> checked_{};
};
}
