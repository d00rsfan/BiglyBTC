// SPDX-License-Identifier: GPL-2.0-only
// Original periods and selection: azrating RatingsUpdater, Aelitis SARL.
#pragma once
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
namespace biglybtc::plugins::rating::publication {
struct ReadHint {
 std::string id;bool complete=false,stopped=false,scored=false;
 std::optional<std::int32_t> scrapeSeeds;
 std::int64_t addedMillis=0;
 bool operator==(const ReadHint&)const=default;
};
class ReadSchedule final {
public:
 static constexpr std::size_t maximumDownloads=8192;
 enum class Failure {Invalid,Limit,Missing,Busy};
 std::expected<void,Failure> update(std::span<const ReadHint>);
 std::expected<void,Failure> request(std::string_view,bool priority);
 std::optional<std::string> take(std::int64_t now);
 void complete(std::int64_t now)noexcept;
 void reset()noexcept;
 std::optional<std::int64_t> nextMillis()const noexcept{return next_;}
 static std::int64_t period(const ReadHint&,std::int64_t now)noexcept;
private:
 struct Entry {ReadHint hint;std::int64_t last=0;bool requested=false;};
 std::map<std::string,Entry,std::less<>> entries_;
 std::optional<std::int64_t> next_;bool active_=false;
};
}
