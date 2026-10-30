// SPDX-License-Identifier: GPL-2.0-or-later
#include "sam_protocol.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace biglybtc::services::i2p::provider {
Command::~Command(){auto found=fields.find("BIGLYBTC_OWNER");if(found!=fields.end()){OPENSSL_cleanse(found->second.data(),found->second.size());}}
std::optional<Command> parseCommand(std::string_view line){
  if(line.empty()||line.size()>MaximumCommandBytes||line.back()!='\n'){return {};}
  line.remove_suffix(1);
  if(std::any_of(line.begin(),line.end(),[](unsigned char c){return c<32||c>126||c=='"'||c=='\'';})){return {};}
  Command out;std::string_view fields;
  const auto prefix=[&](std::string_view value,CommandKind kind){if(line.starts_with(value)){out.kind=kind;fields=line.substr(value.size());return true;}return false;};
  if(!prefix("HELLO VERSION ",CommandKind::Hello)&&!prefix("SESSION CREATE ",CommandKind::Create)
      &&!prefix("NAMING LOOKUP ",CommandKind::Naming)&&!prefix("STREAM CONNECT ",CommandKind::Connect)
      &&!prefix("STREAM ACCEPT ",CommandKind::Accept)){return {};}
  while(!fields.empty()){
    const auto space=fields.find(' ');const auto token=fields.substr(0,space);
    const auto equal=token.find('=');if(equal==std::string_view::npos||equal==0||equal+1==token.size()){return {};}
    auto key=token.substr(0,equal),value=token.substr(equal+1);
    // Destination base64 padding is the only allowed extra '='; token parsing
    // below handles it separately, without accepting a second key assignment.
    if(!out.fields.emplace(key,value).second||out.fields.size()>8){return {};}
    if(space==std::string_view::npos){break;}fields.remove_prefix(space+1);if(fields.empty()){return {};}
  }
  const auto exact=[&](std::initializer_list<std::string_view> keys){if(out.fields.size()!=keys.size()){return false;}return std::all_of(keys.begin(),keys.end(),[&](auto key){return out.fields.contains(key);});};
  bool valid=false;
  switch(out.kind){
    case CommandKind::Hello:valid=exact({"MIN","MAX","BIGLYBTC_OWNER","GENERATION"});break;
    case CommandKind::Create:valid=exact({"STYLE","ID","DESTINATION","PUBLIC_HASH"});break;
    case CommandKind::Naming:valid=exact({"NAME","PURPOSE","TO_PORT","FROM_PORT"});break;
    case CommandKind::Connect:valid=exact({"ID","DESTINATION","PURPOSE","TO_PORT","FROM_PORT"});break;
    case CommandKind::Accept:valid=exact({"ID","PURPOSE","TO_PORT","FROM_PORT"});break;
  }
  return valid?std::optional(std::move(out)):std::nullopt;
}
bool constantEqual(View a,View b)noexcept{return a.size()==b.size()&&(a.empty()||CRYPTO_memcmp(a.data(),b.data(),a.size())==0);}
std::optional<std::uint64_t> decimal(std::string_view text,std::uint64_t maximum){
  if(text.empty()||(text.size()>1&&text.front()=='0')){return {};}
  std::uint64_t n=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),n);
  if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||n>maximum){return {};}return n;
}
std::optional<Bytes> hex(std::string_view text,std::size_t count){
  if(text.size()!=count*2){return {};}Bytes out(count);
  const auto nibble=[](char c)->int{if(c>='0'&&c<='9'){return c-'0';}if(c>='a'&&c<='f'){return c-'a'+10;}return -1;};
  for(std::size_t i=0;i<count;++i){const int a=nibble(text[i*2]),b=nibble(text[i*2+1]);if(a<0||b<0){return {};}out[i]=static_cast<Byte>((a<<4)|b);}return out;
}
std::string hex(View data){static constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(data.size()*2);for(auto b:data){out.push_back(digits[b>>4]);out.push_back(digits[b&15]);}return out;}
std::optional<Hash> hashName(std::string_view name){
  static constexpr std::string_view alphabet="abcdefghijklmnopqrstuvwxyz234567";
  if(name.size()!=60||!name.ends_with(".b32.i2p")){return {};}Hash out{};unsigned buffer=0,bits=0;std::size_t n=0;
  for(char c:name.substr(0,52)){const auto at=alphabet.find(c);if(at==std::string_view::npos){return {};}buffer=(buffer<<5)|static_cast<unsigned>(at);bits+=5;if(bits>=8){bits-=8;if(n>=out.size()){return {};}out[n++]=static_cast<Byte>((buffer>>bits)&255U);}}
  if(n!=out.size()||bits!=4||(buffer&15U)!=0){return {};}return out;
}
std::string hashName(const Hash& hash){static constexpr char alphabet[]="abcdefghijklmnopqrstuvwxyz234567";std::string out;unsigned buffer=0,bits=0;for(auto b:hash){buffer=(buffer<<8)|b;bits+=8;while(bits>=5){bits-=5;out.push_back(alphabet[(buffer>>bits)&31U]);}}if(bits){out.push_back(alphabet[(buffer<<(5-bits))&31U]);}return out+".b32.i2p";}
std::string destinationText(View wire){
  if(wire.empty()||wire.size()>768){return {};}
  std::string out(4*((wire.size()+2)/3),'\0');const int n=EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),wire.data(),static_cast<int>(wire.size()));if(n<0){return {};}out.resize(static_cast<std::size_t>(n));for(char& c:out){if(c=='+'){c='-';}else if(c=='/'){c='~';}}return out;
}
std::optional<Destination> destinationText(std::string_view text){
  if(text.empty()||text.size()>1024||text.size()%4){return {};}
  std::string input(text);std::size_t padding=0;
  if(input.back()=='='){++padding;if(input.size()>1&&input[input.size()-2]=='='){++padding;}}
  for(std::size_t i=0;i<input.size()-padding;++i){char& c=input[i];if(c=='-'){c='+';}else if(c=='~'){c='/';}else if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))){return {};}}
  Bytes data(input.size()/4*3);const int n=EVP_DecodeBlock(data.data(),reinterpret_cast<const unsigned char*>(input.data()),static_cast<int>(input.size()));if(n<0||static_cast<std::size_t>(n)<padding){return {};}data.resize(static_cast<std::size_t>(n)-padding);
  auto result=decodeDestination(data);if(!result||destinationText(result->wire)!=text){return {};}return std::move(*result);
}
} // namespace biglybtc::services::i2p::provider
