// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_seed_values.hpp"
#include "biglybtc/bencode.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <zlib.h>

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
constexpr std::size_t MaxPresenceWire=512, MaxPresenceBody=1024;
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

std::string shortSubscriptionId(const Hash& pubHash) {
  constexpr std::string_view hex="0123456789ABCDEF";
  std::string result;
  for(unsigned i=0;i<10;++i){
    const auto value=static_cast<unsigned char>(pubHash[i]);
    result.push_back(hex[value/16]);result.push_back(hex[value%16]);
  }
  return result;
}

const Node* subscriptionBodyContent(const core::Document& body) {
  const auto* vuze=body.find("vuze");
  const auto* components=vuze?vuze->find("components"):nullptr;
  if (!components||components->type!=Node::Type::List||components->children.size()!=1) { return nullptr; }
  const auto& component=components->children.front();
  const auto* content=component.find("content");
  return integer(component,"type")==16&&content&&content->type==Node::Type::Dictionary?content:nullptr;
}

std::optional<Hash> signedDetailsHash(const Node& details,std::string_view hash,std::size_t size) {
  for (const bool omitAzVersion:{false,true}) {
    const auto encoded=core::encodeValue(sorted(details,omitAzVersion));
    if (!encoded||encoded->size()!=size) { continue; }
    const auto actual=sha1(*encoded);
    if (actual&&hashBytes(*actual)==hash) { return actual; }
  }
  return {};
}

std::string presenceKey(std::string_view shortId,std::uint32_t version) {
  return "subscription:publish:"+std::string(shortId)+':'+std::to_string(version);
}

std::expected<SignedSubscriptionPresence,Error> signedPresence(std::string_view shortId,
    std::string_view signature,const Hash& publication,std::uint32_t version,std::uint32_t signedSize) {
  auto applicationKey=presenceKey(shortId,version);
  const auto dhtKey=sha1(applicationKey);
  if (!dhtKey) { return std::unexpected(Error::Crypto); }
  return SignedSubscriptionPresence{std::move(applicationKey),std::string(signature),publication,version,signedSize,*dhtKey};
}

std::expected<std::string,Error> presenceBody(std::string_view wire) {
  if (wire.size()<2 || wire.size()>=MaxPresenceWire) { return std::unexpected(Error::Limit); }
  const char kind=wire.front();
  wire.remove_prefix(1);
  if (kind==0) { return std::string(wire); }
  if (kind!=1) { return std::unexpected(Error::Malformed); }

  z_stream stream{};
  if (inflateInit2(&stream,MAX_WBITS+16)!=Z_OK) { return std::unexpected(Error::Limit); }
  struct EndInflate {
    z_stream& stream;
    ~EndInflate() { inflateEnd(&stream); }
  } end{stream};
  std::array<unsigned char,MaxPresenceBody> output{};
  stream.next_in=reinterpret_cast<unsigned char*>(const_cast<char*>(wire.data()));
  stream.avail_in=static_cast<uInt>(wire.size());
  stream.next_out=output.data();
  stream.avail_out=output.size();
  if (inflate(&stream,Z_FINISH)!=Z_STREAM_END || stream.avail_in) {
    return std::unexpected(stream.avail_out?Error::Malformed:Error::Limit);
  }
  return std::string(reinterpret_cast<const char*>(output.data()),stream.total_out);
}

std::expected<std::string,Error> cryptMetainfo(const Hash& hash,std::string_view input,bool encrypt) {
  std::array<unsigned char,24> key{};
  std::ranges::copy(hash,key.begin());
  Owned<EVP_CIPHER_CTX,EVP_CIPHER_CTX_free> cipher(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
  if (!cipher||EVP_CipherInit_ex(cipher.get(),EVP_des_ede3_ecb(),nullptr,key.data(),nullptr,encrypt)!=1) {
    return std::unexpected(Error::Crypto);
  }
  std::string output(input.size()+static_cast<std::size_t>(EVP_MAX_BLOCK_LENGTH),'\0');
  auto* decoded=reinterpret_cast<unsigned char*>(output.data());
  int length=0,tail=0;
  if (EVP_CipherUpdate(cipher.get(),decoded,&length,reinterpret_cast<const unsigned char*>(input.data()),
          static_cast<int>(input.size()))!=1||EVP_CipherFinal_ex(cipher.get(),decoded+length,&tail)!=1) {
    return std::unexpected(Error::Crypto);
  }
  output.resize(static_cast<std::size_t>(length+tail));
  return output;
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
  const auto* content=body?subscriptionBodyContent(*body):nullptr;
  const auto* details=content?content->find("details"):nullptr;
  const auto pub=bytes(row,"public_key"), hash=bytes(row,"hash"), sig=bytes(row,"sig");
  const auto size=integer(row,"sig_data_size");
  if(!details||details->type!=Node::Type::Dictionary||details->find("sin_details")
      ||!pub||(pub->size()!=25&&pub->size()!=49)||!hash||hash->size()!=20||!sig||sig->empty()||sig->size()>64
      ||!size||*size<=0||*size>static_cast<std::int64_t>(MaxBytes)
      ||body->originalBytes().size()>static_cast<std::size_t>(*size)+10*1024
      ||(anonymous!=0&&anonymous!=1)||(row.find("is_anonymous")&&!integer(row,"is_anonymous"))
      ||(details->find("is_anonymous")&&!integer(*details,"is_anonymous"))
      ||integer(*details,"is_anonymous").value_or(0)!=anonymous
      ||bytes(*details,"public_key")!=pub||integer(*details,"version")!=version
      ||bytes(*content,"hash")!=hash||bytes(*content,"sig")!=sig||integer(*content,"size")!=size
      ||!core::encodeValue(body->root())||!core::encodeValue(row)) { return std::unexpected(Error::Malformed); }

  const auto signedHash=signedDetailsHash(*details,*hash,static_cast<std::size_t>(*size));
  if(!signedHash||!verify(*signedHash,*pub,*sig,static_cast<std::uint32_t>(*version),static_cast<std::uint32_t>(*size))){
    return std::unexpected(Error::Signature);
  }

  const auto pubHash=sha1(*pub), lookup=sha1(*hash);
  if(!pubHash||!lookup) { return std::unexpected(Error::Crypto); }
  SubscriptionSeed seed;
  seed.publicationHash=*signedHash;seed.lookupKey=*lookup;seed.anonymous=anonymous==1;
  const auto id=shortSubscriptionId(*pubHash);
  seed.fileName=id+".biglybt."+std::to_string(*version);
  // Presence is advertised only for a public definition that also remains
  // public in the held subscription catalog.
  if (integer(*details,"is_public")==1 && (!row.find("is_public") || integer(row,"is_public")==1)) {
    auto presence=signedPresence(id,*sig,*signedHash,static_cast<std::uint32_t>(*version),static_cast<std::uint32_t>(*size));
    if (!presence) { return std::unexpected(presence.error()); }
    seed.presence=std::move(*presence);
  }
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

std::expected<std::string,SubscriptionSeedError> encodeSubscriptionPresence(const SignedSubscriptionPresence& presence,
    std::int64_t publicationNonce) try {
  if (!presence.version || presence.version > MaxVersion || !presence.signedSize || presence.signedSize>MaxBytes
      || presence.signature.empty() || presence.signature.size()>64) {
    return std::unexpected(Error::Malformed);
  }
  auto body="d1:!"+intValue(publicationNonce)+"1:h"+byteString(hashBytes(presence.publicationHash))
      +"1:s"+byteString(presence.signature)+"1:v"+intValue(presence.version)+"1:z"+intValue(presence.signedSize)+'e';
  if (body.size()+1>=MaxPresenceWire) { return std::unexpected(Error::Limit); }
  body.insert(body.begin(),'\0');
  return body;
} catch (const std::bad_alloc&) {
  return std::unexpected(Error::Limit);
}

std::expected<SignedSubscriptionPresence,SubscriptionSeedError> verifySubscriptionPresence(
    std::string_view wire,std::string_view expectedPublicKey,std::uint32_t expectedVersion) try {
  if ((expectedPublicKey.size()!=25 && expectedPublicKey.size()!=49)
      || !expectedVersion || expectedVersion>MaxVersion) {
    return std::unexpected(Error::Malformed);
  }
  auto body=presenceBody(wire);
  if (!body) { return std::unexpected(body.error()); }
  const auto document=core::Document::parse(std::move(*body),{MaxPresenceBody,MaxPresenceBody,32,4});
  if (!document || document->root().type!=Node::Type::Dictionary) { return std::unexpected(Error::Malformed); }

  const auto& fields=document->root();
  const auto hash=bytes(fields,"h"),signature=bytes(fields,"s");
  const auto version=integer(fields,"v"),signedSize=integer(fields,"z");
  if (!hash || hash->size()!=Hash{}.size() || !signature || signature->empty() || signature->size()>64
      || version!=expectedVersion || !signedSize || *signedSize<=0 || *signedSize>static_cast<std::int64_t>(MaxBytes)) {
    return std::unexpected(Error::Malformed);
  }
  Hash signedHash;
  std::ranges::copy(*hash,signedHash.begin());
  const auto size=static_cast<std::uint32_t>(*signedSize);
  if (!verify(signedHash,expectedPublicKey,*signature,expectedVersion,size)) { return std::unexpected(Error::Signature); }
  const auto publicHash=sha1(expectedPublicKey);
  if (!publicHash) { return std::unexpected(Error::Crypto); }
  return signedPresence(shortSubscriptionId(*publicHash),*signature,signedHash,expectedVersion,size);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error::Limit);
}

std::expected<std::string,SubscriptionSeedError> subscriptionSeedDdbResponse(const SubscriptionSeed& seed) try {
  if(seed.metainfo.empty()||seed.metainfo.size()>65536) { return std::unexpected(Error::Limit); }
  auto response=cryptMetainfo(seed.publicationHash,seed.metainfo,true);
  if (response) { response->insert(0,2,'\x01'); }
  return response;
} catch(const std::bad_alloc&) { return std::unexpected(Error::Limit); }

std::expected<std::string,SubscriptionSeedError> openSubscriptionSeedDdbResponse(
    std::string_view wire,const SignedSubscriptionPresence& expected) try {
  if (wire.size()<3||wire.size()>4096) { return std::unexpected(Error::Limit); }
  if (wire[0]!=1||(wire[1]!=0&&wire[1]!=1)||!expected.signedSize||expected.signedSize>MaxBytes) {
    return std::unexpected(Error::Malformed);
  }
  auto body=wire[1]?cryptMetainfo(expected.publicationHash,wire.substr(2),false)
      :std::expected<std::string,Error>{std::string(wire.substr(2))};
  if (!body) { return std::unexpected(body.error()); }
  const auto doc=core::Document::parse(std::move(*body),{4096,4096,256,16});
  const auto* info=doc?doc->find("info"):nullptr;
  if (!info||info->type!=Node::Type::Dictionary) { return std::unexpected(Error::Malformed); }
  constexpr std::string_view prefix="subscription:publish:";
  const std::string_view applicationKey=expected.applicationKey;
  if (!applicationKey.starts_with(prefix)||applicationKey.size()<=prefix.size()+20
      ||applicationKey.substr(prefix.size()+20)!=":"+std::to_string(expected.version)) {
    return std::unexpected(Error::Malformed);
  }
  const auto id=applicationKey.substr(prefix.size(),20);
  if (id.find_first_not_of("0123456789ABCDEF")!=std::string_view::npos) {
    return std::unexpected(Error::Malformed);
  }
  const auto file=std::string(id)+".biglybt."+std::to_string(expected.version);
  const auto length=integer(*info,"length");
  const auto pieces=bytes(*info,"pieces");
  if (bytes(doc->root(),"announce")!="dht:"||doc->find("announce-list")||doc->find("url-list")
      ||doc->find("httpseeds")||doc->find("nodes")||info->find("files")||info->find("meta version")
      ||(info->find("private")&&integer(*info,"private")!=0)||bytes(*info,"hash-override")!=hashBytes(expected.publicationHash)
      ||bytes(*info,"name")!=file||(info->find("name.utf-8")&&bytes(*info,"name.utf-8")!=file)
      ||!length||*length<expected.signedSize||*length>static_cast<std::int64_t>(MaxBytes)||*length>expected.signedSize+10*1024
      ||integer(*info,"piece length")!=PieceBytes||!pieces
      ||pieces->size()!=(static_cast<std::size_t>(*length)+PieceBytes-1)/PieceBytes*Hash{}.size()) {
    return std::unexpected(Error::Malformed);
  }
  // The signed body is verified again after download. Its torrent piece
  // hashes are supplied by this transport and do not sign the subscription.
  return doc->originalBytes();
} catch (const std::bad_alloc&) { return std::unexpected(Error::Limit); }

std::expected<void,SubscriptionSeedError> validateSubscriptionSeedPayload(std::string_view payload,
    const SignedSubscriptionPresence& expected,std::string_view publicKey,bool anonymous) try {
  if (payload.empty()||payload.size()>MaxBytes||!expected.signedSize||expected.signedSize>MaxBytes
      ||payload.size()<expected.signedSize||payload.size()>expected.signedSize+10*1024) {
    return std::unexpected(Error::Limit);
  }
  if ((publicKey.size()!=25&&publicKey.size()!=49)||!expected.version||expected.version>MaxVersion
      ||expected.signature.empty()||expected.signature.size()>64) {
    return std::unexpected(Error::Malformed);
  }
  const auto doc=core::Document::parse(std::string(payload),{MaxBytes,MaxBytes,4096,32});
  const auto* content=doc?subscriptionBodyContent(*doc):nullptr;
  const auto* details=content?content->find("details"):nullptr;
  const auto* format=details?details->find("az_version"):nullptr;
  if (!details||details->type!=Node::Type::Dictionary||details->find("sin_details")
      ||(format&&(format->type!=Node::Type::Integer||format->integer<0||format->integer>1))
      ||!bytes(*details,"name")||!bytes(*details,"json")
      ||bytes(*details,"public_key")!=publicKey||integer(*details,"version")!=expected.version
      ||integer(*details,"is_public")!=1||(details->find("is_anonymous")&&!integer(*details,"is_anonymous"))
      ||integer(*details,"is_anonymous").value_or(0)!=(anonymous?1:0)
      ||bytes(*content,"hash")!=hashBytes(expected.publicationHash)||bytes(*content,"sig")!=expected.signature
      ||integer(*content,"size")!=expected.signedSize) {
    return std::unexpected(Error::Malformed);
  }
  const auto publicHash=sha1(publicKey);
  if (!publicHash) { return std::unexpected(Error::Crypto); }
  const auto versionKey=signedPresence(shortSubscriptionId(*publicHash),expected.signature,expected.publicationHash,
      expected.version,expected.signedSize);
  if (!versionKey) { return std::unexpected(versionKey.error()); }
  if (versionKey->applicationKey!=expected.applicationKey||versionKey->dhtKey!=expected.dhtKey
      ||!signedDetailsHash(*details,hashBytes(expected.publicationHash),expected.signedSize)
      ||!verify(expected.publicationHash,publicKey,expected.signature,expected.version,expected.signedSize)) {
    return std::unexpected(Error::Signature);
  }
  return {};
} catch (const std::bad_alloc&) { return std::unexpected(Error::Limit); }
} // namespace biglybtc::search
