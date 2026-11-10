// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_identity_stamp.hpp"
#include <openssl/evp.h>
#include <set>
namespace biglybtc::storage {
std::expected<ChatIdentityStamp,Error> chatIdentityStamp(const core::Document& cfg,std::string_view key){
 const auto fail=[](){return std::unexpected(Error{ErrorCode::InvalidFormat,"Ambiguous or unreadable chat identity"});};
 const auto& root=cfg.root();if(root.type!=core::Node::Type::Dictionary){return fail();}
 unsigned count=0;for(std::size_t i=0;i<root.children.size();i+=2){count+=root.children[i].bytes==key;}if(count!=1){return fail();}
 const auto* identity=cfg.find(key);if(!identity||identity->type!=core::Node::Type::Dictionary){return fail();}
 std::set<std::string_view> keys;for(std::size_t i=0;i<identity->children.size();i+=2){if(!keys.insert(identity->children[i].bytes).second){return fail();}}
 auto* digest=EVP_MD_CTX_new();if(!digest){return std::unexpected(Error{ErrorCode::Io,"Cannot validate chat identity"});}
 const auto freed=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(digest,EVP_MD_CTX_free);
 if(EVP_DigestInit_ex(digest,EVP_sha256(),nullptr)!=1){return fail();}
 // CryptoECCUtils serialises Java public keys as 49-byte SEC1 points;
 // Native compressed keys use 25 bytes. Fingerprint the exact saved encoding.
 for(const auto* field:{"uid","pub","pri"}){
  const auto* value=identity->find(field);if(!value||value->type!=core::Node::Type::Bytes){return fail();}
  if((std::string_view(field)=="uid"&&value->bytes.size()!=8)||(std::string_view(field)=="pub"&&value->bytes.size()!=25&&value->bytes.size()!=49)||(std::string_view(field)=="pri"&&(value->bytes.empty()||value->bytes.size()>25))){return fail();}
  const auto length=std::to_string(value->bytes.size())+":";
  if(EVP_DigestUpdate(digest,length.data(),length.size())!=1||EVP_DigestUpdate(digest,value->bytes.data(),value->bytes.size())!=1){return fail();}
 }
 ChatIdentityStamp result{};unsigned size=0;if(EVP_DigestFinal_ex(digest,result.data(),&size)!=1||size!=result.size()){return fail();}return result;
}
}
