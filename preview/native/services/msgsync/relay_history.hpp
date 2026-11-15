// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "message.hpp"
#include <openssl/evp.h>
namespace biglybtc::services::msgsync {
// Actual RPC relay contact, never an unsigned message contact hint. The caller
// snapshots the request's new-message count before dispatching concurrent calls.
struct RelayHistory {std::string contact;bool record=false;};
inline Result<std::string> relayHistory(std::string_view previous,const RelayHistory& relay){
 if(previous.size()>MaximumHistoryBytes){return std::unexpected(Error::Limit);}
 if(!relay.record){return std::string(previous);}
 if(relay.contact.empty()||relay.contact.size()>1024){return std::unexpected(Error::Input);}
 std::array<unsigned char,20> hash{};unsigned length=0;
 if(EVP_Digest(relay.contact.data(),relay.contact.size(),hash.data(),&length,EVP_sha1(),nullptr)!=1||length!=hash.size()){return std::unexpected(Error::Crypto);}
 std::string out(reinterpret_cast<const char*>(hash.data()),4);out.append(previous.substr(0,MaximumHistoryBytes-4));return out;
}
}
