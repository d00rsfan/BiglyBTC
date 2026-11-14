// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_profile.hpp"
#include "azureus_profile_io.hpp"
#include <variant>
#include <deque>
#include <functional>
namespace biglybtc::app {
struct ChatProfileReadMarker {std::string key;std::optional<std::string> expected;std::string value;storage::ChatIdentityStamp stamp{};std::function<bool()> live;};
struct ChatProfileList {};
struct ChatProfilePreferences {std::string key;std::vector<std::string> publicKeys;};
enum class ChatPreferenceField {Ratings,Subscriptions,Ignored,Pinned};
// Issued by the owner only after actual signed-roster membership validation.
// The held IO gate rechecks the room epoch and original identity before writing.
struct ChatProfilePreference {std::string key,publicKey;ChatPreferenceField field;bool expected=false,value=false;storage::ChatIdentityStamp stamp{};std::function<bool()> live;};
struct ChatProfileLoad {std::string key;bool create=false;};
struct ChatProfileOptions {ChatRoomSettings settings;std::optional<bool> expectedIndications={};std::optional<storage::ChatIdentityStamp> identity={};std::function<bool()> live={};};
struct ChatProfilePrivatePolicy {std::int32_t expected=3,value=3;};
struct ChatProfileNotifications {ChatNotificationSettings expected,value;};
struct ChatProfileNicknameMode {std::string key;bool shared=true;};
struct ChatProfileNickname {std::string key,nickname;std::optional<bool> expectedShared{};bool instance=false;std::optional<storage::ChatIdentityStamp> expectedIdentity={};std::function<bool()> live={};};
struct ChatProfileHistory {std::string key,encoded;std::optional<storage::ChatIdentityStamp> expectedIdentity{};};
using ChatProfileRequest=std::variant<ChatProfileList,ChatProfileLoad,ChatProfileOptions,ChatProfileHistory,ChatProfileNicknameMode,ChatProfileNickname,ChatProfilePreferences,ChatProfilePreference,ChatProfileReadMarker,ChatProfileNotifications,ChatProfilePrivatePolicy>;
struct ChatProfileData {std::optional<std::int32_t> privateChatState={};
 std::vector<ChatRoomSettings> catalogue;
 std::shared_ptr<ChatIdentity> identity;
 std::optional<std::string> history;
 std::optional<storage::Error> historyError;
 std::optional<ChatPreferences> preferences;
 std::optional<ChatNotificationSettings> notifications;
 std::optional<std::string> readMarker;
};
struct ChatProfileCompletion {std::uint64_t id=0,generation=0;std::string key;std::expected<ChatProfileData,storage::Error> result;};
// Private room-owner facade over the existing held-gate I/O broker. Metadata
// is delivered only after its worker has released the source gate. All facade
// methods except join are called on the room Qt thread. Lifetime callers join
// outside the source gate after requestStop; no Qt/network thread waits on IO.
class ChatProfileIo final {
public:
 using Clock=std::chrono::steady_clock;
 explicit ChatProfileIo(std::shared_ptr<const storage::ProfileLease>);
 ~ChatProfileIo();ChatProfileIo(const ChatProfileIo&)=delete;
 void bind(QObject*);
 std::expected<std::uint64_t,storage::Error> submit(ChatProfileRequest,std::uint64_t generation,Clock::time_point end);
 bool cancel(std::uint64_t);
 std::vector<ChatProfileCompletion> advance();
 void requestStop()noexcept;
 void join()noexcept;
 static QEvent::Type wakeEvent(){return detail::AzureusProfileIo::wakeEvent();}
private:
 using Broker=detail::AzureusProfileIo;
 using Outcome=std::expected<ChatProfileData,storage::Error>;
 struct Box {std::optional<Outcome> outcome;};
 struct Pending {std::uint64_t id,generation;std::string key;Clock::time_point end;Broker::Handle job;std::shared_ptr<Box> box;std::optional<Outcome> outcome;};
 std::shared_ptr<ChatProfile> profile_;std::unique_ptr<Broker> io_;
 std::deque<Pending> pending_;std::uint64_t next_=0;bool stopping_=false;
};
}
