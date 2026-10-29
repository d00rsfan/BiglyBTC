// SPDX-License-Identifier: GPL-2.0-or-later
// Wire/crypto reference: I2P2.13.0, 9134f808337b401e8e53c73734c81fab04280c9d,
// Datagram2/3, Destination, SigningPublicKey, CryptoConstants, DSAEngine/SigUtil.
#include "datagram.hpp"
#include <algorithm>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/dsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
namespace biglybtc::services::i2p {
namespace {
template<class T,auto Free>using Owned=std::unique_ptr<T,decltype(Free)>;
using Key=Owned<EVP_PKEY,EVP_PKEY_free>;
using Number=Owned<BIGNUM,BN_clear_free>;
using Context=Owned<EVP_MD_CTX,EVP_MD_CTX_free>;
std::unexpected<Error> error(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
constexpr const char* Prime="9c05b2aa960d9b97b8931963c9cc9e8c3026e9b8ed92fad0a69cc886d5bf8015fcadae31a0ad18fab3f01b00a358de237655c4964afaa2b337e96ad316b9fb1cc564b5aec5b69a9ff6c3e4548707fef8503d91dd8602e867e6d35d2235c1869ce2479c3b9d5401de04e0727fb33d6511285d4cf29538d9e3b6051f5b22cc1c93";
constexpr const char* Order="a5dfc28fef4ca1e286744cd8eed9d29d684046b7";
constexpr const char* Generator="c1f4d27d40093b429e962d7223824e0bbc47e7c832a39236fc683af84889581075ff9082ed32353d4374d7301cda1d23c431f4698599dda02451824ff369752593647cc3ddc197de985e43d136cdcfc6bd5409cd2f450821142a5e6f8eb1c3ab5d0484b8129fcf17bce4f7f33321c3cb3dbb14a905e7b2b3e93be4708cbcc82";
Number hex(const char* value){BIGNUM* raw=nullptr;if(!BN_hex2bn(&raw,value)){BN_clear_free(raw);raw=nullptr;}return Number(raw,BN_clear_free);}
Number number(View value,bool secret=false){
  Number out(secret?BN_secure_new():BN_new(),BN_clear_free);
  if(!out||!BN_bin2bn(value.data(),static_cast<int>(value.size()),out.get())){return Number(nullptr,BN_clear_free);}return out;
}
Result<Hash> hash(View bytes){Hash result{};unsigned int size=0;
  if(EVP_Digest(bytes.data(),bytes.size(),result.data(),&size,EVP_sha256(),nullptr)!=1||size!=result.size()){return error(ErrorCode::CryptoFailure,"I2P destination SHA-256 failed");}return result;
}
std::size_t privateSize(SigningType type){return type==SigningType::DsaSha1?20:32;}
std::size_t signatureSize(SigningType type){return type==SigningType::DsaSha1?40:64;}
Result<Key> key(SigningType type,View publicKey,View privateKey={}){
  if(type==SigningType::Ed25519){
    EVP_PKEY* raw=privateKey.empty()?EVP_PKEY_new_raw_public_key_ex(nullptr,"ED25519",nullptr,publicKey.data(),publicKey.size())
        :EVP_PKEY_new_raw_private_key_ex(nullptr,"ED25519",nullptr,privateKey.data(),privateKey.size());
    if(!raw){return error(ErrorCode::CryptoFailure,"I2P Ed25519 key import failed");}return Key(raw,EVP_PKEY_free);
  }
  auto p=hex(Prime),q=hex(Order),g=hex(Generator),y=number(publicKey),x=number(privateKey,true);
  Owned<OSSL_PARAM_BLD,OSSL_PARAM_BLD_free> builder(OSSL_PARAM_BLD_new(),OSSL_PARAM_BLD_free);
  if(!p||!q||!g||!y||!builder||!OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_FFC_P,p.get())
      ||!OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_FFC_Q,q.get())||!OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_FFC_G,g.get())
      ||!OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_PUB_KEY,y.get())
      ||(!privateKey.empty()&&(!x||!OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_PRIV_KEY,x.get())))){return error(ErrorCode::CryptoFailure,"I2P DSA key parameters failed");}
  Owned<OSSL_PARAM,OSSL_PARAM_free> parameters(OSSL_PARAM_BLD_to_param(builder.get()),OSSL_PARAM_free);
  Owned<EVP_PKEY_CTX,EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new_from_name(nullptr,"DSA",nullptr),EVP_PKEY_CTX_free);EVP_PKEY* raw=nullptr;
  if(!parameters||!context||EVP_PKEY_fromdata_init(context.get())!=1
      ||EVP_PKEY_fromdata(context.get(),&raw,privateKey.empty()?EVP_PKEY_PUBLIC_KEY:EVP_PKEY_KEYPAIR,parameters.get())!=1){EVP_PKEY_free(raw);return error(ErrorCode::CryptoFailure,"I2P DSA key import failed");}
  return Key(raw,EVP_PKEY_free);
}
Result<Bytes> derSignature(View raw){
  if(raw.size()!=40){return error(ErrorCode::InvalidFormat,"I2P DSA signature length changed");}
  auto r=number(raw.first(20)),s=number(raw.last(20));Owned<DSA_SIG,DSA_SIG_free> sig(DSA_SIG_new(),DSA_SIG_free);
  if(!r||!s||!sig||DSA_SIG_set0(sig.get(),r.get(),s.get())!=1){return error(ErrorCode::CryptoFailure,"I2P DSA signature conversion failed");}(void)r.release();(void)s.release();
  const auto size=i2d_DSA_SIG(sig.get(),nullptr);if(size<=0){return error(ErrorCode::CryptoFailure,"I2P DSA signature encoding failed");}Bytes out(static_cast<std::size_t>(size));auto* at=out.data();
  if(i2d_DSA_SIG(sig.get(),&at)!=size){return error(ErrorCode::CryptoFailure,"I2P DSA signature encoding changed");}return out;
}
Result<Bytes> rawSignature(View der){
  const auto* at=der.data();Owned<DSA_SIG,DSA_SIG_free> sig(d2i_DSA_SIG(nullptr,&at,static_cast<long>(der.size())),DSA_SIG_free);
  if(!sig||at!=der.data()+der.size()){return error(ErrorCode::CryptoFailure,"I2P DSA signature DER failed");}
  const BIGNUM *r=nullptr,*s=nullptr;DSA_SIG_get0(sig.get(),&r,&s);Bytes out(40);
  if(!r||!s||BN_bn2binpad(r,out.data(),20)!=20||BN_bn2binpad(s,out.data()+20,20)!=20){return error(ErrorCode::CryptoFailure,"I2P DSA signature width changed");}return out;
}
Result<Bytes> sign(SigningType type,View pub,View priv,View data){
  auto loaded=key(type,pub,priv);if(!loaded){return std::unexpected(loaded.error());}Context ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);std::size_t size=0;
  if(!ctx||EVP_DigestSignInit(ctx.get(),nullptr,type==SigningType::DsaSha1?EVP_sha1():nullptr,nullptr,loaded->get())!=1
      ||EVP_DigestSign(ctx.get(),nullptr,&size,data.data(),data.size())!=1||!size||size>128){return error(ErrorCode::CryptoFailure,"I2P datagram signer initialization failed");}
  Bytes signature(size);if(EVP_DigestSign(ctx.get(),signature.data(),&size,data.data(),data.size())!=1){return error(ErrorCode::CryptoFailure,"I2P datagram signing failed");}signature.resize(size);
  if(type==SigningType::DsaSha1){return rawSignature(signature);}if(signature.size()!=64){return error(ErrorCode::CryptoFailure,"I2P Ed25519 signature width changed");}return signature;
}
Result<void> verify(SigningType type,View pub,View signature,View data){
  auto loaded=key(type,pub);if(!loaded){return std::unexpected(loaded.error());}Bytes converted;
  if(type==SigningType::DsaSha1){auto der=derSignature(signature);if(!der){return std::unexpected(der.error());}converted=std::move(*der);signature=converted;}
  Context ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!ctx||EVP_DigestVerifyInit(ctx.get(),nullptr,type==SigningType::DsaSha1?EVP_sha1():nullptr,nullptr,loaded->get())!=1){return error(ErrorCode::CryptoFailure,"I2P datagram verifier initialization failed");}
  if(EVP_DigestVerify(ctx.get(),signature.data(),signature.size(),data.data(),data.size())!=1){return error(ErrorCode::BadSignature,"I2P datagram signature failed");}return {};
}
std::uint16_t u16(View bytes,std::size_t at){return static_cast<std::uint16_t>((std::uint16_t(bytes[at])<<8)|bytes[at+1]);}
Result<Destination> destination(View bytes,bool exact){
  if(bytes.size()<387){return error(ErrorCode::InvalidFormat,"I2P destination is truncated");}const auto length=u16(bytes,385);const auto size=387+std::size_t(length);
  if(size>1024){return error(ErrorCode::TooLarge,"I2P destination certificate exceeds bounded scope");}
  if(bytes.size()<size||(exact&&bytes.size()!=size)){return error(ErrorCode::InvalidFormat,"I2P destination size changed");}
  const auto cert=bytes[384];std::uint16_t signature=0,encryption=0;
  if(cert==0){if(length){return error(ErrorCode::InvalidFormat,"I2P null certificate has a payload");}}
  else if(cert==5){if(length!=4){return error(ErrorCode::Unsupported,"I2P extended key certificate is unsupported");}signature=u16(bytes,387);encryption=u16(bytes,389);}
  else{return error(ErrorCode::Unsupported,"I2P destination certificate type is unsupported");}
  if((signature!=0&&signature!=7)||(encryption!=0&&encryption!=4)){return error(ErrorCode::Unsupported,"I2P signing/encryption type is outside default codec scope");}
  const auto keySize=signature==0?128:32;const auto raw=bytes.first(size);auto digest=hash(raw);if(!digest){return std::unexpected(digest.error());}
  return Destination{Bytes(raw.begin(),raw.end()),static_cast<SigningType>(signature),encryption,Bytes(raw.begin()+384-keySize,raw.begin()+384),*digest};
}
Result<void> flags(View bytes,std::size_t offset,Byte version){
  if(bytes.size()-offset<2||(bytes[offset+1]&15)!=version){return error(ErrorCode::InvalidFormat,"I2P datagram version/header is invalid");}
  if(bytes[offset]||(bytes[offset+1]&0xf0)){return error(ErrorCode::Unsupported,"I2P datagram options/offline/reserved flags are unsupported");}return {};
}
} // namespace
Result<Destination> decodeDestination(View bytes){return destination(bytes,true);}
Result<Bytes> signingPublicKey(SigningType type,View privateKey){
  if(type!=SigningType::DsaSha1&&type!=SigningType::Ed25519){return error(ErrorCode::Unsupported,"I2P signing type is unsupported");}
  if(privateKey.size()!=privateSize(type)){return error(ErrorCode::KeyMismatch,"I2P private signing key has the wrong length");}
  if(type==SigningType::Ed25519){auto loaded=key(type,{},privateKey);if(!loaded){return std::unexpected(loaded.error());}Bytes pub(32);std::size_t size=pub.size();
    if(EVP_PKEY_get_raw_public_key(loaded->get(),pub.data(),&size)!=1||size!=32){return error(ErrorCode::CryptoFailure,"I2P Ed25519 public key derivation failed");}return pub;}
  auto p=hex(Prime),q=hex(Order),g=hex(Generator),x=number(privateKey,true);Number y(BN_new(),BN_clear_free);Owned<BN_CTX,BN_CTX_free> ctx(BN_CTX_new(),BN_CTX_free);
  if(!p||!q||!g||!x||!y||!ctx){return error(ErrorCode::CryptoFailure,"I2P DSA public key allocation failed");}
  if(BN_is_zero(x.get())||BN_cmp(x.get(),q.get())>=0){return error(ErrorCode::KeyMismatch,"I2P DSA private exponent is invalid");}
  if(BN_mod_exp_mont_consttime(y.get(),g.get(),x.get(),p.get(),ctx.get(),nullptr)!=1){return error(ErrorCode::CryptoFailure,"I2P DSA public key derivation failed");}Bytes pub(128);
  if(BN_bn2binpad(y.get(),pub.data(),128)!=128){return error(ErrorCode::CryptoFailure,"I2P DSA public key width changed");}return pub;
}
Result<Bytes> signData(SigningType type,View privateKey,View data){
  if(data.size()>MaximumSignedDataBytes){return error(ErrorCode::TooLarge,"I2P signing input exceeds protocol bound");}
  auto pub=signingPublicKey(type,privateKey);if(!pub){return std::unexpected(pub.error());}
  return sign(type,*pub,privateKey,data);
}
Result<void> verifyData(SigningType type,View publicKey,View signature,View data){
  if(data.size()>MaximumSignedDataBytes){return error(ErrorCode::TooLarge,"I2P signing input exceeds protocol bound");}
  if(type!=SigningType::DsaSha1&&type!=SigningType::Ed25519){return error(ErrorCode::Unsupported,"I2P signing type is unsupported");}
  if(publicKey.size()!=(type==SigningType::DsaSha1?128U:32U)||signature.size()!=signatureSize(type)){
    return error(ErrorCode::InvalidFormat,"I2P public signing key/signature width changed");
  }
  return verify(type,publicKey,signature,data);
}
Result<Bytes> encode2(View sender,View privateKey,const Hash& recipient,View payload){
  auto from=decodeDestination(sender);if(!from){return std::unexpected(from.error());}const auto overhead=sender.size()+2+signatureSize(from->signingType);
  if(payload.size()>MaximumWireBytes-overhead){return error(ErrorCode::TooLarge,"I2P Datagram2 exceeds maker bound");}
  auto pub=signingPublicKey(from->signingType,privateKey);if(!pub){return std::unexpected(pub.error());}if(*pub!=from->signingPublicKey){return error(ErrorCode::KeyMismatch,"I2P private key does not match sender signing key");}
  Bytes data(34+payload.size());std::copy(recipient.begin(),recipient.end(),data.begin());data[32]=0;data[33]=2;
  std::copy(payload.begin(),payload.end(),data.begin()+34);auto signature=sign(from->signingType,*pub,privateKey,data);if(!signature){return std::unexpected(signature.error());}
  Bytes wire(sender.begin(),sender.end());wire.insert(wire.end(),data.begin()+32,data.end());wire.insert(wire.end(),signature->begin(),signature->end());return wire;
}
Result<Datagram2> decode2(View wire,const Hash& recipient){
  if(wire.size()>MaximumWireBytes){return error(ErrorCode::TooLarge,"I2P Datagram2 exceeds bounded input");}auto from=destination(wire,false);if(!from){return std::unexpected(from.error());}
  const auto off=from->wire.size(),siglen=signatureSize(from->signingType);if(wire.size()<off+2+siglen){return error(ErrorCode::InvalidFormat,"I2P Datagram2 payload/signature is truncated");}
  auto supported=flags(wire,off,2);if(!supported){return std::unexpected(supported.error());}const auto tail=wire.subspan(off,wire.size()-off-siglen);
  Bytes data(recipient.begin(),recipient.end());data.insert(data.end(),tail.begin(),tail.end());auto checked=verify(from->signingType,from->signingPublicKey,wire.last(siglen),data);if(!checked){return std::unexpected(checked.error());}
  return Datagram2{std::move(*from),Bytes(tail.begin()+2,tail.end())};
}
Result<Bytes> encode3(View sender,View payload){
  if(payload.size()>MaximumWireBytes-34){return error(ErrorCode::TooLarge,"I2P Datagram3 exceeds maker bound");}auto from=decodeDestination(sender);if(!from){return std::unexpected(from.error());}
  Bytes wire(from->hash.begin(),from->hash.end());wire.push_back(0);wire.push_back(3);wire.insert(wire.end(),payload.begin(),payload.end());return wire;
}
Result<Datagram3> decode3(View wire){
  if(wire.size()<34){return error(ErrorCode::InvalidFormat,"I2P Datagram3 header is truncated");}if(wire.size()>MaximumWireBytes){return error(ErrorCode::TooLarge,"I2P Datagram3 exceeds bounded input");}
  auto supported=flags(wire,32,3);if(!supported){return std::unexpected(supported.error());}Hash claimed{};std::copy_n(wire.begin(),32,claimed.begin());return Datagram3{claimed,Bytes(wire.begin()+34,wire.end())};
}
} // namespace biglybtc::services::i2p
