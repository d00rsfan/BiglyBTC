// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "chat_identity_stamp.hpp"
#include "room.hpp"
#include "attention_policy.hpp"
#include <span>
#include <map>
namespace biglybtc::app {
struct ChatRoomSettings {std::string key,nickname;bool favourite=false,saveHistory=false,readOnly=false,sharedNickname=true;std::optional<std::string> lastMessageInfo={};std::string defaultNickname={};bool disableIndicators=false;};
struct ChatIdentity {ChatRoomSettings settings;services::msgsync::Uid uid;plugins::msgsync::MessageSigner signer;storage::ChatIdentityStamp sourceStamp{};};
using ChatParticipantSettings=services::msgsync::ChatParticipantSettings;
using ChatPreferences=services::msgsync::ChatPreferences;
using ChatNotificationSettings=services::msgsync::ChatNotificationSettings;
enum class ChatDisplayField {Ratings,Subscriptions};
enum class ChatParticipantField {Ignored,Pinned};
class ChatProfile final {
public:
 static std::expected<std::unique_ptr<ChatProfile>,storage::Error> open(const storage::ProfileLease&);
 std::expected<std::vector<ChatRoomSettings>,storage::Error> rooms();
 std::expected<ChatPreferences,storage::Error> preferences(std::span<const std::string> publicKeys={});
 std::expected<bool,storage::Error> saveDisplay(ChatDisplayField,bool expected,bool value);
 std::expected<bool,storage::Error> saveParticipant(std::string publicKey,ChatParticipantField,bool expected,bool value);
 static std::expected<std::string,storage::Error> participantKey(std::string_view publicKey);
 std::expected<ChatIdentity,storage::Error> identity(std::string key,bool create);
 std::expected<bool,storage::Error> saveReadMarker(std::string key,std::optional<std::string> expected,std::string value);
 std::expected<void,storage::Error> saveOptions(const ChatRoomSettings&,std::optional<bool> expectedIndications={});
 std::expected<bool,storage::Error> saveNotifications(const ChatNotificationSettings& expected,const ChatNotificationSettings& value);
 std::expected<void,storage::Error> setSharedNickname(std::string_view key,bool shared);
 std::expected<void,storage::Error> setInstanceNickname(std::string_view key,std::string nickname);
 std::expected<void,storage::Error> setNickname(std::string_view key,std::string nickname,std::optional<bool> expectedShared={});
 std::expected<std::optional<std::string>,storage::Error> history(std::string_view key);
 std::expected<void,storage::Error> saveHistory(std::string_view key,std::string encoded);
 const storage::ProfileLease& lease()const noexcept{return *lease_;}
 static std::string configKey(std::string_view key);
 static std::string optionKey(std::string_view key);
 static std::string historyLeaf(std::string_view key);
private:
 explicit ChatProfile(std::shared_ptr<const storage::ProfileLease> lease):lease_(std::move(lease)){}
 struct History {std::optional<std::string> primary;std::string original;bool writable=true;};
 std::shared_ptr<const storage::ProfileLease> lease_;std::map<std::string,History> histories_;
};
}
