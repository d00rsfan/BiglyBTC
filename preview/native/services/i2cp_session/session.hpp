// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "identity.hpp"
#include "protocol.hpp"
#include <utility>

namespace biglybtc::services::i2p::i2cp {
template<class T>using ConfigResult=::biglybtc::services::i2p::Result<T>;
using SessionOptions=std::vector<std::pair<std::string,std::string>>;
inline constexpr Byte CreateSessionType=1,ReconfigureSessionType=2;
inline constexpr std::size_t MaximumPropertiesBytes=65535;
inline constexpr std::size_t MaximumSessionConfigBytes=1024+2+MaximumPropertiesBytes+8+64;
inline constexpr std::uint64_t SessionConfigDateTolerance=180000;
struct SessionConfig {
  Destination destination;
  SessionOptions options;
  std::uint64_t creationMilliseconds=0;
  Bytes signature;
};
// Pure DTOs, never channel/admission authority. Reconfigure's sessionId is
// outside the signed preimage, exactly as in the original wire format.
struct CreateSession {SessionConfig config;};
struct ReconfigureSession {std::uint16_t sessionId=0;SessionConfig config;};
using SessionMessage=std::variant<CreateSession,ReconfigureSession>;

// Valid UTF-8, sorted by original Java UTF-16 String order, with unique keys.
// Per-string255 encoded bytes, original uint16 properties body limit. Unknown
// router options preserved; offline/authentication keys explicitly unsupported.
[[nodiscard]] ConfigResult<SessionOptions> canonicalSessionOptions(const SessionOptions&);
// Mirrors producer's client-only filtering and >255 Java-char removal before
// SessionConfig's byte-length validation. No profile lookup/default promotion.
[[nodiscard]] ConfigResult<SessionOptions> projectRouterOptions(const SessionOptions& clientOptions);
// Original reconfigure is putAll, never replacement/removal of omitted keys.
[[nodiscard]] ConfigResult<SessionOptions> mergeSessionOptions(const SessionOptions& current,const SessionOptions& update);

// No clock reads/updates. Caller supplies the creation date and verification
// clock explicitly; clock freshness and signature checks are separate APIs.
[[nodiscard]] ConfigResult<Bytes> sessionConfigPreimage(const SessionConfig&);
[[nodiscard]] ConfigResult<SessionConfig> signSessionConfig(const ClientIdentity&,const SessionOptions&,std::uint64_t creationMilliseconds);
[[nodiscard]] ConfigResult<void> verifySessionConfigSignature(const SessionConfig&);
[[nodiscard]] ConfigResult<void> verifySessionConfig(const SessionConfig&,std::uint64_t currentMilliseconds);
[[nodiscard]] ConfigResult<Bytes> encodeSessionConfig(const SessionConfig&);
[[nodiscard]] ConfigResult<SessionConfig> decodeSessionConfig(View);
[[nodiscard]] ConfigResult<Bytes> encodeSessionMessage(const SessionMessage&);
[[nodiscard]] ConfigResult<SessionMessage> decodeSessionMessage(const Frame&);
} // namespace biglybtc::services::i2p::i2cp
