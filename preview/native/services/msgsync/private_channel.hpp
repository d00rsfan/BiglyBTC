// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "message.hpp"
#include "envelope.hpp"
namespace biglybtc::services::msgsync {
using PrivateActivity=std::array<std::uint8_t,8>;
using PrivateUserKey=std::array<std::uint8_t,16>;
enum class PrivatePhase {Keys,Auth};
struct PrivateRequest {
 PrivateActivity activity{};PrivateUserKey userKey{};PrivatePhase phase=PrivatePhase::Keys;
 std::string packet;
};
struct PrivateReply {std::optional<std::string> keys,auth,error;};
// Plain bencoded DH messages carried only in the parent's general encryption.
Result<PrivateRequest> decodePrivateRequest(std::string);
Result<std::string> encodePrivateRequest(const PrivateRequest&);
Result<PrivateReply> decodePrivateReply(std::string);
Result<std::string> encodePrivateReply(const PrivateReply&);
// A typed private child, never a public room key or DHT registration. Only the
// two completed STS paths can construct it. No unauthenticated key export.
class PrivateChannel final {
public:
 ~PrivateChannel();PrivateChannel(PrivateChannel&&)noexcept;
 PrivateChannel& operator=(PrivateChannel&&)noexcept;
 PrivateChannel(const PrivateChannel&)=delete;
 PrivateChannel& operator=(const PrivateChannel&)=delete;
 std::string_view peerKey()const noexcept{return peer_;}
 PrivateUserKey userKey()const noexcept{return user_;}
 std::string_view handler()const noexcept;
 Result<std::string> encrypt(std::string_view)const;
 Result<std::string> decrypt(std::string_view)const;
private:
 PrivateChannel(PrivateUserKey,std::string,std::array<std::uint8_t,24>,std::array<std::uint8_t,20>);
 static Result<PrivateChannel> authenticated(plugins::msgsync::LegacySts&,PrivateUserKey,std::string_view);
 void clear()noexcept;
 PrivateUserKey user_{};std::string peer_;
 std::array<std::uint8_t,24> secret_{};std::array<std::uint8_t,20> handler_{};
 bool alive_=true;
 friend class PrivateInitiator;friend class PrivateResponder;
};
// Single worker owners. A failure consumes the exchange. The caller must bind
// parent epoch, actual target/contact and fresh source authority at every effect.
class PrivateInitiator final {
public:
 static Result<PrivateInitiator> create(plugins::msgsync::LegacySts,std::string expectedPeer);
 PrivateInitiator(PrivateInitiator&&)noexcept=default;
 PrivateInitiator& operator=(PrivateInitiator&&)noexcept=default;
 Result<PrivateRequest> first()const;
 Result<PrivateRequest> keysReply(const PrivateReply&);
 Result<PrivateChannel> finish(const PrivateReply&);
private:
 PrivateInitiator(plugins::msgsync::LegacySts,std::string,PrivateActivity,PrivateUserKey);
 std::optional<plugins::msgsync::LegacySts> sts_;std::string peer_;
 PrivateActivity activity_;PrivateUserKey user_;bool waitingFinal_=false;
};
class PrivateResponder final {
public:
 static Result<PrivateResponder> create(plugins::msgsync::LegacySts,const PrivateRequest&);
 PrivateResponder(PrivateResponder&&)noexcept=default;
 PrivateResponder& operator=(PrivateResponder&&)noexcept=default;
 Result<PrivateReply> first()const;
 // Authenticated sender still requires the actual parent roster/accept policy.
 // Do not register/admit a child or reply success until that owner accepts it.
 Result<PrivateChannel> finish(const PrivateRequest&);
private:
 PrivateResponder(plugins::msgsync::LegacySts,PrivateActivity,PrivateUserKey);
 std::optional<plugins::msgsync::LegacySts> sts_;
 PrivateActivity activity_;PrivateUserKey user_;
};
}
