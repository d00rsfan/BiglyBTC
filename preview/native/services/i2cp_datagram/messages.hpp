// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "session.hpp"
namespace biglybtc::services::i2p::i2cp {
inline constexpr Byte SendMessageType=5,SendMessageExpiresType=36,MessageStatusType=22,MessagePayloadType=31;
inline constexpr std::size_t MaximumMuxDatagramBytes=40*1024,MaximumI2cpPayloadBytes=64*1024;
enum class GzipMode {Stored,Deflate}; // Explicit original GZIP_OFF / level3.
struct MuxDatagram {Byte protocol=Datagram2Protocol;std::uint16_t fromPort=0,toPort=0;Bytes datagram;};
struct SendDatagram {std::uint16_t sessionId=0;Destination recipient;Bytes payload;std::uint32_t nonce=0;std::optional<std::uint64_t> expirationMilliseconds;};
struct MessageStatus {std::uint16_t sessionId=0;std::uint32_t messageId=0;Byte code=0;std::uint32_t size=0,nonce=0;};
enum class StatusMeaning {IncomingAvailable,RouterAccepted,UnusedLegacySuccess,ProbableSuccess,LocalSuccess,ProbableFailure,Failure};
struct MessagePayload {std::uint16_t sessionId=0;std::uint32_t messageId=0;Bytes payload;};
[[nodiscard]] ConfigResult<Bytes> encodeMuxDatagram(const MuxDatagram&,GzipMode);
[[nodiscard]] ConfigResult<MuxDatagram> decodeMuxDatagram(View);
[[nodiscard]] ConfigResult<Bytes> encodeSendDatagram(const SendDatagram&);
[[nodiscard]] ConfigResult<SendDatagram> decodeSendDatagram(const Frame&);
[[nodiscard]] ConfigResult<Bytes> encodeMessageStatus(const MessageStatus&);
[[nodiscard]] ConfigResult<MessageStatus> decodeMessageStatus(const Frame&);
[[nodiscard]] ConfigResult<StatusMeaning> messageStatusMeaning(Byte);
[[nodiscard]] ConfigResult<Bytes> encodeMessagePayload(const MessagePayload&);
[[nodiscard]] ConfigResult<MessagePayload> decodeMessagePayload(const Frame&);
// Original router captures this at initial destination creation; updates do
// not alter it. No trimming or invented coercion. No saved defaults applied.
[[nodiscard]] bool originalStatusRepliesEnabled(const SessionOptions&) noexcept;
} // namespace biglybtc::services::i2p::i2cp
