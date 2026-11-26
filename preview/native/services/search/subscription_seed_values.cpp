// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_seed_values.hpp"
#include "biglybtc/bencode.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace biglybtc::search {
namespace {
using core::Node;
using Error=SubscriptionSeedError;
template<class T,void(*Release)(T*)>
using Owned=std::unique_ptr<T,decltype(Release)>;

constexpr std::size_t MaxBytes=4*1024*1024, PieceBytes=256*1024;
constexpr std::int64_t MaxVersion=std::numeric_limits<std::int32_t>::max();
using Hash=std::array<char,20>;

std::optional<std::int64_t> integer(const Node& row,std::string_view key) {
  const auto* node=row.find(key);
  if(!node||node->type!=Node::Type::Integer) { return {}; }
  return node->integer;
}

std::optional<std::string_view> bytes(const Node& row,std::string_view key) {
  const auto* node=row.find(key);
  if(!node||node->type!=Node::Type::Bytes) { return {}; }
  return node->bytes;
}

std::optional<Hash> sha1(std::string_view value) {
  Hash hash;
  std::size_t size=0;
  if(!EVP_Q_digest(nullptr,"SHA1",nullptr,value.data(),value.size(),
      reinterpret_cast<unsigned char*>(hash.data()),&size)||size!=hash.size()) { return {}; }
  return hash;
}

std::string_view hashBytes(const Hash& hash) { return {hash.data(),hash.size()}; }

Node sorted(const Node& source,bool omitAzVersion=false) {
  Node result(source.type);
  result.bytes=source.bytes;result.integer=source.integer;
  result.children.reserve(source.children.size());
  if(source.type==Node::Type::List) {
    for(const auto& child:source.children){result.children.push_back(sorted(child));}
  }else if(source.type==Node::Type::Dictionary){
    std::vector<std::size_t> order(source.children.size()/2);
    std::iota(order.begin(),order.end(),0);
    std::ranges::sort(order,{},[&](std::size_t index){return source.children[2*index].bytes;});
    for(const auto index:order){
      const auto& key=source.children[2*index];
      if(omitAzVersion&&key.bytes=="az_version"){continue;}
      result.children.push_back(key);
      result.children.push_back(sorted(source.children[2*index+1]));
    }
  }
  return result;
}

std::string byteString(std::string_view value) { return std::to_string(value.size())+":"+std::string(value); }
std::string intValue(std::int64_t value) { return "i"+std::to_string(value)+"e"; }

bool verify(const Hash& hash,std::string_view pub,std::string_view sig,std::uint32_t version,std::uint32_t size) {
  Owned<OSSL_PARAM_BLD,OSSL_PARAM_BLD_free> builder(OSSL_PARAM_BLD_new(),OSSL_PARAM_BLD_free);
  Owned<EVP_PKEY_CTX,EVP_PKEY_CTX_free> import(EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr),EVP_PKEY_CTX_free);
  if(!builder||!import||!OSSL_PARAM_BLD_push_utf8_string(builder.get(),OSSL_PKEY_PARAM_GROUP_NAME,"prime192v2",0)
      ||!OSSL_PARAM_BLD_push_octet_string(builder.get(),OSSL_PKEY_PARAM_PUB_KEY,pub.data(),pub.size())) { return false; }
  Owned<OSSL_PARAM,OSSL_PARAM_free> parameters(OSSL_PARAM_BLD_to_param(builder.get()),OSSL_PARAM_free);
  EVP_PKEY* raw=nullptr;
  if(!parameters||EVP_PKEY_fromdata_init(import.get())!=1) { return false; }
  const auto loaded=EVP_PKEY_fromdata(import.get(),&raw,EVP_PKEY_PUBLIC_KEY,parameters.get());
  Owned<EVP_PKEY,EVP_PKEY_free> key(raw,EVP_PKEY_free);
  if(loaded!=1||!key) { return false; }
  Owned<EVP_MD_CTX,EVP_MD_CTX_free> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!digest||EVP_DigestVerifyInit(digest.get(),nullptr,EVP_sha1(),nullptr,key.get())!=1) { return false; }
  std::string signedValue(hashBytes(hash));
  for(const auto value:{version,size}){
    for(int shift=24;shift>=0;shift-=8){signedValue.push_back(static_cast<char>((value>>shift)&255));}
  }
  return EVP_DigestVerify(digest.get(),reinterpret_cast<const unsigned char*>(sig.data()),sig.size(),
      reinterpret_cast<const unsigned char*>(signedValue.data()),signedValue.size())==1;
}

std::string seedFileName(const Hash& pubHash,std::int64_t version) {
  constexpr std::string_view hex="0123456789ABCDEF";
  std::string result;
  for(unsigned i=0;i<10;++i){
    const auto value=static_cast<unsigned char>(pubHash[i]);
    result.push_back(hex[value/16]);result.push_back(hex[value%16]);
  }
  return result+".biglybt."+std::to_string(version);
}
} // namespace

std::expected<SubscriptionSeed,SubscriptionSeedError> makeSubscriptionSeed(
    const Node& row,std::string vuzeBytes,std::int64_t creationSeconds) try {
  const auto version=integer(row,"version");
  const auto subscribed=integer(row,"subscribed");
  const auto anonymous=integer(row,"is_anonymous").value_or(0);
  if(subscribed!=1||!version||*version<=0||*version>MaxVersion||row.find("sin_details")){
    return std::unexpected(Error::Ineligible);
  }
  if(vuzeBytes.empty()||vuzeBytes.size()>MaxBytes||creationSeconds<0) { return std::unexpected(Error::Limit); }
  auto body=core::Document::parse(std::move(vuzeBytes));
  const auto* vuze=body?body->find("vuze"):nullptr;
  const auto* components=vuze?vuze->find("components"):nullptr;
  if(!components||components->type!=Node::Type::List||components->children.size()!=1) { return std::unexpected(Error::Malformed); }
  const auto& component=components->children.front();
  const auto* content=component.find("content");
  const auto* details=content?content->find("details"):nullptr;
  const auto pub=bytes(row,"public_key"), hash=bytes(row,"hash"), sig=bytes(row,"sig");
  const auto size=integer(row,"sig_data_size");
  if(integer(component,"type")!=16||!details||details->type!=Node::Type::Dictionary||details->find("sin_details")
      ||!pub||(pub->size()!=25&&pub->size()!=49)||!hash||hash->size()!=20||!sig||sig->empty()||sig->size()>64
      ||!size||*size<=0||*size>static_cast<std::int64_t>(MaxBytes)
      ||body->originalBytes().size()>static_cast<std::size_t>(*size)+10*1024
      ||(anonymous!=0&&anonymous!=1)||(row.find("is_anonymous")&&!integer(row,"is_anonymous"))
      ||(details->find("is_anonymous")&&!integer(*details,"is_anonymous"))
      ||integer(*details,"is_anonymous").value_or(0)!=anonymous
      ||bytes(*details,"public_key")!=pub||integer(*details,"version")!=version
      ||bytes(*content,"hash")!=hash||bytes(*content,"sig")!=sig||integer(*content,"size")!=size
      ||!core::encodeValue(body->root())||!core::encodeValue(row)) { return std::unexpected(Error::Malformed); }

  std::optional<Hash> signedHash;
  for(const bool omitAzVersion:{false,true}){
    auto encoded=core::encodeValue(sorted(*details,omitAzVersion));
    if(!encoded||encoded->size()!=static_cast<std::size_t>(*size)) { continue; }
    signedHash=sha1(*encoded);
    if(signedHash&&hashBytes(*signedHash)==*hash) { break; }
    signedHash.reset();
  }
  if(!signedHash||!verify(*signedHash,*pub,*sig,static_cast<std::uint32_t>(*version),static_cast<std::uint32_t>(*size))){
    return std::unexpected(Error::Signature);
  }

  const auto pubHash=sha1(*pub), lookup=sha1(*hash);
  if(!pubHash||!lookup) { return std::unexpected(Error::Crypto); }
  SubscriptionSeed seed;
  seed.publicationHash=*signedHash;seed.lookupKey=*lookup;seed.anonymous=anonymous==1;
  seed.fileName=seedFileName(*pubHash,*version);
  const std::string_view payload=body->originalBytes();
  std::string pieces;
  for(std::size_t offset=0;offset<payload.size();offset+=PieceBytes){
    const auto piece=sha1(payload.substr(offset,PieceBytes));
    if(!piece) { return std::unexpected(Error::Crypto); }
    pieces.append(hashBytes(*piece));
  }
  const auto leaf=byteString(seed.fileName);
  seed.metainfo="d8:announce4:dht:18:azureus_propertiesd20:dht_backup_requestedi1ee13:creation date"
      +intValue(creationSeconds)+"4:infod13:hash-override"+byteString(*hash)+"6:length"
      +intValue(static_cast<std::int64_t>(payload.size()))+"4:name"+leaf+"10:name.utf-8"+leaf
      +"12:piece length"+intValue(PieceBytes)+"6:pieces"+byteString(pieces)+"ee";
  return seed;
} catch(const std::bad_alloc&) { return std::unexpected(Error::Limit); }

std::expected<std::string,SubscriptionSeedError> subscriptionSeedDdbResponse(const SubscriptionSeed& seed) try {
  if(seed.metainfo.empty()||seed.metainfo.size()>65536) { return std::unexpected(Error::Limit); }
  std::array<unsigned char,24> key{};
  std::ranges::copy(seed.publicationHash,key.begin());
  Owned<EVP_CIPHER_CTX,EVP_CIPHER_CTX_free> cipher(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
  if(!cipher||EVP_EncryptInit_ex(cipher.get(),EVP_des_ede3_ecb(),nullptr,key.data(),nullptr)!=1) {
    return std::unexpected(Error::Crypto);
  }
  std::string response(seed.metainfo.size()+static_cast<std::size_t>(EVP_MAX_BLOCK_LENGTH)+2,'\0');
  response[0]=1;response[1]=1;
  auto* output=reinterpret_cast<unsigned char*>(response.data())+2;
  int encrypted=0,padding=0;
  if(EVP_EncryptUpdate(cipher.get(),output,&encrypted,reinterpret_cast<const unsigned char*>(seed.metainfo.data()),
      static_cast<int>(seed.metainfo.size()))!=1||EVP_EncryptFinal_ex(cipher.get(),output+encrypted,&padding)!=1){
    return std::unexpected(Error::Crypto);
  }
  response.resize(static_cast<std::size_t>(encrypted+padding)+2);
  return response;
} catch(const std::bad_alloc&) { return std::unexpected(Error::Limit); }
} // namespace biglybtc::search
