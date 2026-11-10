// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "message.hpp"

namespace biglybtc::services::msgsync {
enum class AddOutcome {Added,Duplicate,Tombstoned,TooOld};
struct LedgerEntry {Message message;std::string senderKey;std::uint64_t ordinal=0;std::int64_t timestamp=0;};
struct ContactBloomKey {std::string_view publicKey,originalAddressUtf8;};
// Pure single-owner bookkeeping after mathematical signature verification.
// Room/profile/network/admission, moderation and persistence are separate.
class MessageLedger final {
public:
  MessageLedger();
  Result<AddOutcome> add(VerifiedMessage,std::int64_t wallMilliseconds,bool loading=false);
  Result<std::vector<LedgerEntry>> snapshot(std::int64_t wallMilliseconds);
  Result<SyncBloom> buildBloom(std::span<const ContactBloomKey>)const;
  std::size_t size()const noexcept{return messages_.size();}
  std::size_t tombstones()const noexcept{return inverted_.size();}
  std::int32_t mutations()const noexcept{return mutations_;}
  std::int32_t newMessages()const noexcept{return newMessages_;}
  void clear()noexcept;
private:
  struct Stored {VerifiedMessage verified;std::int64_t received;std::int32_t age;std::uint64_t ordinal=0;std::int64_t timestamp=0;};
  static std::int32_t age(Stored&,std::int64_t)noexcept;
  std::vector<Stored> messages_;
  std::vector<std::string> inverted_;
  std::int32_t mutations_=0,newMessages_=0;std::uint64_t ordinal_=0;
};
}
