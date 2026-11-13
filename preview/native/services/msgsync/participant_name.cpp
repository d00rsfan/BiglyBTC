// SPDX-License-Identifier: GPL-2.0-or-later
#include "participant_name.hpp"
namespace biglybtc::services::msgsync {
std::optional<std::string> participantShortName(std::optional<std::string_view> key){
 if(key&&key->size()<11){return {};}
 std::string out(6,'0');constexpr char hex[]="0123456789ABCDEF";
 if(key){for(unsigned i=0;i<3;++i){const auto byte=static_cast<unsigned char>((*key)[8+i]);out[2*i]=hex[byte>>4];out[2*i+1]=hex[byte&15];}}
 return out;
}
}
