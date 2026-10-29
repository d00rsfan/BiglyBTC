// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace biglybtc::services::i2p::i2cp {
using Byte=std::uint8_t;
using Bytes=std::vector<Byte>;
using View=std::span<const Byte>;
enum class ErrorCode {InvalidFormat,Unsupported,TooLarge,Truncated,Failed};
struct Error {ErrorCode code;std::string message;};
template<class T>using Result=std::expected<T,Error>;
inline constexpr std::array<Byte,1> ClientGreeting{0x2a};
inline constexpr std::size_t FrameHeaderBytes=5;
inline constexpr std::size_t MaximumPayloadBytes=128*1024; // original MAX_LENGTH
inline constexpr std::size_t MaximumFrameBytes=FrameHeaderBytes+MaximumPayloadBytes;

// Length is four unsigned big-endian bytes, EXCLUDING the one-byte type.
// Framing is opaque; unknown types receive no session/dispatch authority.
struct Frame {Byte type=0;Bytes payload;bool operator==(const Frame&)const=default;};
[[nodiscard]] Result<Bytes> encodeFrame(Byte type,View payload);
[[nodiscard]] Result<Frame> decodeFrame(View exactFrame);

struct FrameStep {std::size_t consumed=0;std::optional<Frame> frame;};
// Consumes only enough input to return at most ONE owned frame; any suffix
// remains with the caller. No callback, unbounded output queue or borrowed data.
// Errors/end-of-input truncation poison this stream until explicit reset().
class FrameDecoder {
public:
  [[nodiscard]] Result<FrameStep> consume(View input);
  [[nodiscard]] Result<void> finish();
  void reset() noexcept;
  [[nodiscard]] bool failed()const noexcept{return failed_;}
  [[nodiscard]] std::size_t bufferedBytes()const noexcept{return headerUsed_+payloadUsed_;}
private:
  std::array<Byte,FrameHeaderBytes> header_{};
  std::size_t headerUsed_=0,payloadLength_=0,payloadUsed_=0;
  bool lengthKnown_=false,failed_=false;
  Bytes payload_;
};

inline constexpr Byte DestroySessionType=3,SessionStatusType=20,DisconnectType=30,GetDateType=32,SetDateType=33;
struct GetDate {std::optional<std::string> version;bool operator==(const GetDate&)const=default;};
// Raw nonnegative Java-date milliseconds. Zero decodes to original null Date;
// it is NOT a valid clock sample/authority. No wall-clock updates occur here.
struct SetDate {std::uint64_t milliseconds=0;std::optional<std::string> version;bool operator==(const SetDate&)const=default;};
struct Disconnect {std::string reason;bool operator==(const Disconnect&)const=default;};
enum class SessionState:Byte {Destroyed=0,Created=1,Updated=2,Invalid=3,Refused=4};
struct SessionStatus {std::uint16_t sessionId=0;SessionState state=SessionState::Invalid;bool operator==(const SessionStatus&)const=default;};
struct DestroySession {std::uint16_t sessionId=0;bool operator==(const DestroySession&)const=default;};
using Message=std::variant<GetDate,SetDate,Disconnect,SessionStatus,DestroySession>;

// Bounded initial NO-AUTH subset. ASCII strings only: original writeString
// truncates Java chars to bytes whereas readString uses UTF-8. GetDate options,
// internal DUP_DEST status5, negative dates and all other types are unsupported.
// Parsing a message never creates/accepts a session, updates clocks or connects.
[[nodiscard]] Result<Bytes> encodeMessage(const Message&);
[[nodiscard]] Result<Message> decodeMessage(const Frame&);
} // namespace biglybtc::services::i2p::i2cp
