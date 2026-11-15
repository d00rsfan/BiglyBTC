// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace biglybtc::services::azureus_key_block {
struct Record {
 std::string request,signature;std::int32_t received=0;bool direct=false;
 bool add()const noexcept{return !request.empty()&&request[0]==1;}
 std::int32_t created()const noexcept;
 std::string_view key()const noexcept{return request.size()>8?std::string_view(request).substr(8):std::string_view{};}
 bool operator==(const Record&)const=default;
};
struct Decision {bool verify=false,replace=false,returnBlocked=false;bool operator==(const Decision&)const=default;};
// A plan is not verified metadata, a mutation, or network authority.
Decision decide(const Record* previous,const Record& incoming)noexcept;
struct RsaPublicKey {std::string_view modulus,exponent;};
RsaPublicKey officialPublicKey()noexcept;
// Protocol primitive, also used with generated test keys. Owners use verifyOfficial.
enum class Verification {Valid,Invalid,Malformed,Unavailable};
Verification verifySignatureStatus(std::string_view,std::string_view,RsaPublicKey)noexcept;
Verification verifyOfficialStatus(std::string_view,std::string_view)noexcept;
bool verifySignature(std::string_view request,std::string_view signature,RsaPublicKey)noexcept;
bool verifyOfficial(std::string_view request,std::string_view signature)noexcept;
}
