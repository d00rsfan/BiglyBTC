// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ledger.hpp"
#include "envelope.hpp"
#include <deque>
namespace biglybtc::services::msgsync {
struct RoomMessage {
  MessageId id{};Uid sender{};std::string publicKey,text,nickname;
  std::int32_t ageSeconds=0;bool local=false;std::optional<MessageId> previous;
  std::optional<std::int64_t> sequence;
};
struct ReceiveSummary {std::size_t added=0,duplicates=0,rejected=0;std::int32_t more=0;};
// Single chat-worker owner. No sockets/profile/Qt and no implicit room changes.
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
  Result<ReceiveSummary> receive(std::string envelope,std::int64_t now);
  Result<MessageId> send(std::string text,std::string nickname,std::int64_t now);
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
  plugins::msgsync::ChannelKeys keys_;std::string localContact_;bool readOnly_=false;
  MessageLedger ledger_;RequestReplay replay_;
  struct Known {Uid uid;std::string key,contact;};std::deque<Known> known_;
  std::optional<MessageId> previous_;std::int64_t sequence_=0;
};
}
