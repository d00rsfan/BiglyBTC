// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::services::i2p {
using Byte=std::uint8_t;
using Bytes=std::vector<Byte>;
using View=std::span<const Byte>;
using Hash=std::array<Byte,32>;
enum class ErrorCode {InvalidFormat,Unsupported,TooLarge,KeyMismatch,BadSignature,CryptoFailure};
struct Error {ErrorCode code;std::string message;};
template<class T>using Result=std::expected<T,Error>;
enum class SigningType:std::uint16_t {DsaSha1=0,Ed25519=7};
inline constexpr std::size_t MaximumWireBytes=61*1024;
inline constexpr std::uint8_t Datagram2Protocol=19,Datagram3Protocol=20;
struct Destination {
  Bytes wire;
  SigningType signingType;
  std::uint16_t encryptionType;
  Bytes signingPublicKey;
  Hash hash;
};
// Null certificate/DSA or exact four-byte key certificate (signature0/7,
// encryption0/4). Raw encryption key/padding are preserved, never interpreted
// as a router/client/session authority. No network or persistent identities.
Result<Destination> decodeDestination(View);
Result<Bytes> signingPublicKey(SigningType,View privateKey);
struct Datagram2 {Destination sender;Bytes payload;};
// Online/default no-options form. Receiver hash participates in the signature
// but is NOT transmitted. Signature proves signing-key possession/recipient
// binding, not authenticity of every sender destination/padding byte.
Result<Bytes> encode2(View senderDestination,View privateKey,const Hash& recipient,View payload);
Result<Datagram2> decode2(View wire,const Hash& currentRecipient);
struct Datagram3 {Hash claimedSender;Bytes payload;};
// UNSIGNED format: no authentication or destination-resolution rights. A
// future session layer must authenticate/bind claimedSender independently.
Result<Bytes> encode3(View senderDestination,View payload);
Result<Datagram3> decode3(View wire);
} // namespace biglybtc::services::i2p
