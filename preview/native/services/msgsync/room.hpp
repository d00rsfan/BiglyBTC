// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ledger.hpp"
#include "envelope.hpp"
#include <deque>
#include <memory>
namespace biglybtc::services::msgsync {
struct RoomPolicy {std::string baseKey;std::optional<std::string> manager;bool readOnly=false;};
Result<RoomPolicy> roomPolicy(std::string_view);
struct SyncAnswer {std::string envelope;Uid requester{};bool self=false;};
struct RoomMessage {
  MessageId id{};Uid sender{};std::string publicKey,text,nickname;
  std::int32_t ageSeconds=0;bool local=false;std::optional<MessageId> previous;
  std::optional<std::int64_t> sequence;std::optional<std::int64_t> timestamp{};std::int32_t origin=0;
};
struct ReceiveSummary {std::size_t added=0,duplicates=0,rejected=0;std::int32_t more=0;std::optional<NodeHint> hint{};};
// Single chat-worker owner. No sockets/profile/Qt and no implicit room changes.
class PreparedSend final {
public:
 PreparedSend(PreparedSend&&)noexcept=default;PreparedSend& operator=(PreparedSend&&)noexcept=default;
 PreparedSend(const PreparedSend&)=delete;PreparedSend& operator=(const PreparedSend&)=delete;
private:
 PreparedSend(std::weak_ptr<const char> owner,std::int32_t mutation,VerifiedMessage value):owner_(std::move(owner)),mutation_(mutation),value_(std::make_unique<VerifiedMessage>(std::move(value))){}
 std::weak_ptr<const char> owner_;std::int32_t mutation_;std::unique_ptr<VerifiedMessage> value_;
 friend class Room;
};
class Room final {
public:
  static Result<Room> open(std::string userKey,Uid,plugins::msgsync::MessageSigner,
      std::string encodedLocalContact,bool readOnly=false);
  ~Room();Room(Room&&)noexcept;Room& operator=(Room&&)noexcept;
  Room(const Room&)=delete;Room& operator=(const Room&)=delete;
  std::string_view key()const noexcept{return userKey_;}
  std::string_view handler()const noexcept{return {reinterpret_cast<const char*>(keys_.listen.data()),keys_.listen.size()};}
  std::string_view publicKey()const noexcept{return signer_.publicKey();}
  Uid uid()const noexcept{return uid_;}
  Result<std::string> request(std::int64_t now);
  Result<std::string> answer(std::string envelope,std::int64_t now);
  Result<SyncAnswer> answerDetailed(std::string envelope,std::int64_t now,std::optional<NodeHint> hint={});
  Result<ReceiveSummary> receive(std::string envelope,std::int64_t now);
  // Signing runs outside a held profile source permit. The move-only result
  // cannot enter the ledger until a caller commits it under fresh authority.
  Result<PreparedSend> prepareSend(std::string text,std::string nickname,std::int64_t now,std::optional<std::int32_t> origin={});
  Result<MessageId> commitSend(PreparedSend&&,std::int64_t now);
  Result<MessageId> send(std::string text,std::string nickname,std::int64_t now,std::optional<std::int32_t> origin={});
  Result<std::vector<RoomMessage>> messages(std::int64_t now);
  Result<std::string> exportHistory(std::int64_t now);
  Result<ReceiveSummary> importHistory(std::string encoded,std::int64_t now);
  std::size_t retainedMessages()const noexcept{return ledger_.size();}
  std::size_t retainedKeys()const noexcept{return known_.size();}
  bool readOnly()const noexcept{return readOnly_;}
  void updateContact(std::string contact){localContact_=std::move(contact);}
private:
  Room(std::string,Uid,plugins::msgsync::MessageSigner,plugins::msgsync::ChannelKeys,std::string,bool);
  Result<ReceiveSummary> admit(std::vector<Message>,std::int64_t,bool loading=false);
  std::string userKey_;Uid uid_{};plugins::msgsync::MessageSigner signer_;
  plugins::msgsync::ChannelKeys keys_;std::string localContact_;bool readOnly_=false,managedReadOnly_=false;std::optional<std::string> manager_;
  std::shared_ptr<const char> sendOwner_=std::make_shared<const char>(0);
  MessageLedger ledger_;RequestReplay replay_;
  struct Known {Uid uid;std::string key,contact;};std::deque<Known> known_;
  std::vector<std::uint64_t> displayOrder_;
};
}
