// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::app {
struct ActivityReference {std::string id,stamp;bool operator==(const ActivityReference&)const=default;};
struct ActivityEntry {
 ActivityReference source;std::string type,icon,text,callbackClass;
 std::vector<std::string> actions;std::map<std::string,std::string> callbackData;
 std::int64_t timestamp=0,readOn=0;bool viewed=false;
 bool publicChat()const;
};
struct ActivitySnapshot {std::vector<ActivityEntry> entries;bool recovered=false,truncated=false;std::size_t malformed=0;};
// Only the future source owner may construct posts from an actual signed Public
// room. Storage applies data validation; it grants no networking authority.
struct ChatActivityPost {std::string key,text,action="View";std::int64_t timestamp=0;bool privateChat=false;};
class ActivityProfile final {
public:
 static constexpr auto Leaf="VuzeActivities.config";
 static constexpr std::int64_t MaximumAge=2LL*365*24*60*60*1000;
 static std::expected<ActivitySnapshot,storage::Error> read(const storage::ProfileLease&,std::int64_t now);
 static std::expected<ActivitySnapshot,storage::Error> post(const storage::ProfileLease&,std::span<const ChatActivityPost>,std::int64_t now);
 static std::expected<ActivitySnapshot,storage::Error> readState(const storage::ProfileLease&,std::span<const ActivityReference>,bool read,std::int64_t now);
 static std::expected<ActivitySnapshot,storage::Error> viewed(const storage::ProfileLease&,std::span<const ActivityReference>,std::int64_t now);
 static std::expected<ActivitySnapshot,storage::Error> remove(const storage::ProfileLease&,std::span<const ActivityReference>,std::int64_t now);
};
}
