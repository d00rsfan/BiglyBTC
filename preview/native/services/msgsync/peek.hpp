// SPDX-License-Identifier: GPL-2.0-only
// Original MsgSyncHandler/MsgSyncPlugin peek payloads, Vuze, Inc.
#pragma once
#include "bloom.hpp"
#include "envelope.hpp"
#include <optional>
namespace biglybtc::services::msgsync {
using PeekKey=std::array<std::uint8_t,20>;
struct PeekRequest {std::string random;};
struct PeekReply {
 std::int32_t messages=0,live=0,estimate=0;std::optional<std::int32_t> total;
 std::optional<std::string> signature,publicKey;
};
Result<std::string> encodePeekRequest(const PeekKey&,std::string_view random);
Result<PeekRequest> decodePeekRequest(std::string,const PeekKey& expected);
Result<std::string> encodePeekReply(const PeekReply&);
Result<PeekReply> decodePeekReply(std::string);
// No profile, identity, discovery or sender authority. Caller authenticates an
// actual transfer receipt and freshly permits every network/profile effect.
// Identity-free original General AES channel. Deriving these shared room keys
// neither joins a room nor establishes a participant/public-key identity.
class PeekChannel final {
public:
 static Result<PeekChannel> open(std::string_view userKey);
 ~PeekChannel();PeekChannel(PeekChannel&&)noexcept=default;
 PeekChannel& operator=(PeekChannel&&)noexcept=default;
 PeekChannel(const PeekChannel&)=delete;PeekChannel& operator=(const PeekChannel&)=delete;
 std::string_view handler()const noexcept{return {reinterpret_cast<const char*>(keys_.peek.data()),keys_.peek.size()};}
 const PeekKey& discoveryKey()const noexcept{return keys_.listen;}
 Result<std::string> request()const;
 Result<PeekReply> receive(std::string envelope)const;
 Result<std::string> answer(std::string envelope,const PeekReply&)const;
private:
 explicit PeekChannel(plugins::msgsync::ChannelKeys keys):keys_(keys){}
 plugins::msgsync::ChannelKeys keys_;
};
struct PeekSummary {std::int32_t messages=0,nodes=0;std::optional<std::string> signature,publicKey;unsigned results=0;};
class PeekAggregate final {
public:
 bool add(const PeekReply&); // false after the original third accepted result
 const PeekSummary& summary()const noexcept{return summary_;}
private:PeekSummary summary_;std::int32_t live_=0,estimate_=0;
};
}
