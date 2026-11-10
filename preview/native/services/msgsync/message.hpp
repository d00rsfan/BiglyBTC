// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bloom.hpp"
#include "legacy_sts.hpp"
#include <array>
#include <optional>

namespace biglybtc::services::msgsync {
using Uid=std::array<std::uint8_t,8>;
using MessageId=std::array<std::uint8_t,8>;
inline constexpr std::size_t MaximumMessages=128,MaximumTombstones=128,
    MaximumContentBytes=600,MaximumControlBytes=4096,MaximumHistoryBytes=80,
    MaximumReplyContentBytes=4096,MaximumTransferBytes=16*1024,
    MaximumWireBytes=MaximumTransferBytes-32, // Original IV16 + full PKCS7 block.
    MaximumDecodedBytes=64*1024;
struct Message {
  Uid uid{};
  MessageId id{};
  std::string content;
  std::optional<std::string> control;
  std::string signature;
  std::int32_t ageSeconds=0; // Original intValue narrowing; constructor clamps negative.
  std::string history;
  // Unsigned optional roster/contact hints, NOT proof of sender/contact rights.
  std::optional<std::string> publicKey,encodedContact;
};
// A mathematical signature result only. The operational owner must separately
// bind actual network/room/profile/contact/source/generation on every effect.
class VerifiedMessage final {
public:
  const Message& message()const noexcept{return message_;}
  std::string_view senderKey()const noexcept{return sender_;}
private:
  VerifiedMessage(Message,std::string);
  Message message_;
  std::string sender_;
  friend Result<VerifiedMessage> verifyMessage(Message,std::string_view);
};
Result<Message> decodeMessage(std::string);
Result<std::string> encodeMessage(const Message&);
Result<std::string> signedBytes(const Message&);
Result<VerifiedMessage> verifyMessage(Message,std::string_view actualCandidatePublicKey);
Result<Message> signMessage(const plugins::msgsync::MessageSigner&,Uid,
    std::string content,std::optional<std::string> control={});

struct SyncRequest {
  std::int32_t version=5;
  Uid uid{};
  std::optional<std::array<std::uint8_t,6>> requestId;
  std::array<std::uint8_t,8> random{};
  AddOnlyBloom bloom;
  std::int32_t messages=0,newMessages=0;
  std::optional<std::int32_t> oldestAgeSeconds;
};
// Unsigned routing suggestion, not a verified participant or endpoint grant.
struct NodeHint {Uid uid{};std::string encodedContact;};
struct SyncReply {
  std::int32_t status=1; // Original OK1 / self-loopback2, not application delivery.
  std::vector<Message> messages;
  std::int32_t moreToCome=0;
  std::optional<NodeHint> hint{};
};
Result<SyncRequest> decodeSyncRequest(std::string);
Result<std::string> encodeSyncRequest(const SyncRequest&);
Result<SyncReply> decodeSyncReply(std::string);
Result<std::string> encodeSyncReply(const SyncReply&,bool allowGzip=true);

// Original request_id_history:512 access-ordered keys; q6 or legacy r8 fallback.
// Pure bookkeeping only, never an endpoint/session/operation correlation token.
class RequestReplay final {
public:
  RequestReplay();
  Result<bool> remember(const SyncRequest&); // false: duplicate, touches its LRU position.
  std::size_t size()const noexcept{return keys_.size();}
  void clear()noexcept{keys_.clear();}
private:
  std::vector<std::string> keys_;
};
}
