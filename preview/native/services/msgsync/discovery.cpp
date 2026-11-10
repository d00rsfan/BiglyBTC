// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTPluginStorageManager diversification: Azureus Software, Inc., cb8c6cb9.
#include "discovery.hpp"
#include <openssl/evp.h>
#include <algorithm>
namespace biglybtc::services::msgsync {
Result<DiscoveryKey> diversifiedDiscoveryKey(const DiscoveryKey& key,unsigned offset){
 if(offset>=10){return std::unexpected(Error::Input);}std::array<std::uint8_t,21> input;std::copy(key.begin(),key.end(),input.begin());input.back()=static_cast<std::uint8_t>(offset);DiscoveryKey result;unsigned length=0;
 if(EVP_Digest(input.data(),input.size(),result.data(),&length,EVP_sha1(),nullptr)!=1||length!=20){return std::unexpected(Error::Crypto);}return result;
}
Result<std::vector<DiscoveryKey>> diversifiedDiscoveryKeys(const DiscoveryKey& key,std::int8_t type,std::span<const unsigned> offsets){
 if((type!=2&&type!=3)||offsets.size()!=(type==2?1U:2U)||(offsets.size()==2&&offsets[0]==offsets[1])){return std::unexpected(Error::Input);}std::vector<DiscoveryKey> result;
 for(const auto offset:offsets){auto derived=diversifiedDiscoveryKey(key,offset);if(!derived){return std::unexpected(derived.error());}result.push_back(*derived);}return result;
}
}
