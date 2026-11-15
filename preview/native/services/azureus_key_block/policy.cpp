// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#include "policy.hpp"
#include <bit>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>
namespace biglybtc::services::azureus_key_block {
std::int32_t Record::created()const noexcept{std::uint32_t n=0;if(request.size()<8){return 0;}for(unsigned i=4;i<8;++i){n=(n<<8)|static_cast<unsigned char>(request[i]);}return std::bit_cast<std::int32_t>(n);}
Decision decide(const Record* previous,const Record& incoming)noexcept{
 if(incoming.request.size()<=8||incoming.request.size()>255){return {};}
 if(previous&&((previous->direct&&!incoming.direct)||previous->created()>incoming.created())){return {};}
 if(incoming.add()){const bool insert=!previous||!previous->add();return {insert,insert,true};}
 const bool insert=incoming.direct&&(!previous||previous->add());return {insert,insert,false};
}
RsaPublicKey officialPublicKey()noexcept{return {"b8a440c76405b2175a24c86d70f2c71929673a31045791d8bd84220a48729998900d227b560e88357074fa534ccccc6944729bfdda5413622f068e7926176a8afc8b75d4ba6cde760096624415b544f73677e8093ddba46723cb973b4d55f61c2003b73f52582894c018e141e8d010bb615cdbbfaeb97a7af6ce1a5a20a62994da81bde6487e8a39e66c8df0cfd9d763c2da4729cbf54278ea4912169edb0a33","10001"};}
Verification verifySignatureStatus(std::string_view request,std::string_view signature,RsaPublicKey publicKey)noexcept{
 try{
  if(request.size()>255||signature.empty()||signature.size()>8192){return Verification::Malformed;}
  if(publicKey.modulus.empty()||publicKey.modulus.size()>2048||publicKey.exponent.empty()||publicKey.exponent.size()>16){return Verification::Unavailable;}
  const std::string modulus(publicKey.modulus),exponent(publicKey.exponent);BIGNUM *rawN=nullptr,*rawE=nullptr;
  const auto nCount=BN_hex2bn(&rawN,modulus.c_str());std::unique_ptr<BIGNUM,decltype(&BN_free)> n(rawN,BN_free);
  const auto eCount=BN_hex2bn(&rawE,exponent.c_str());std::unique_ptr<BIGNUM,decltype(&BN_free)> e(rawE,BN_free);
  if(nCount!=static_cast<int>(modulus.size())||eCount!=static_cast<int>(exponent.size())||!n||!e||BN_is_negative(n.get())||BN_is_zero(n.get())||BN_is_negative(e.get())||BN_is_zero(e.get())){return Verification::Unavailable;}
  std::unique_ptr<OSSL_PARAM_BLD,decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(),OSSL_PARAM_BLD_free);
  if(!builder||OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_RSA_N,n.get())!=1||OSSL_PARAM_BLD_push_BN(builder.get(),OSSL_PKEY_PARAM_RSA_E,e.get())!=1){return Verification::Unavailable;}
  std::unique_ptr<OSSL_PARAM,decltype(&OSSL_PARAM_free)> parameters(OSSL_PARAM_BLD_to_param(builder.get()),OSSL_PARAM_free);
  std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> build(EVP_PKEY_CTX_new_from_name(nullptr,"RSA",nullptr),EVP_PKEY_CTX_free);EVP_PKEY* rawKey=nullptr;
  if(!parameters||!build||EVP_PKEY_fromdata_init(build.get())!=1){return Verification::Unavailable;}
  const auto imported=EVP_PKEY_fromdata(build.get(),&rawKey,EVP_PKEY_PUBLIC_KEY,parameters.get());std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(rawKey,EVP_PKEY_free);if(imported!=1||!key){return Verification::Unavailable;}
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);EVP_PKEY_CTX* verify=nullptr;
  if(!digest||EVP_DigestVerifyInit(digest.get(),&verify,EVP_md5(),nullptr,key.get())!=1||!verify||EVP_PKEY_CTX_set_rsa_padding(verify,RSA_PKCS1_PADDING)!=1){return Verification::Unavailable;}
  if(signature.size()!=static_cast<std::size_t>(EVP_PKEY_get_size(key.get()))){return Verification::Malformed;}
  const auto status=EVP_DigestVerify(digest.get(),reinterpret_cast<const unsigned char*>(signature.data()),signature.size(),reinterpret_cast<const unsigned char*>(request.data()),request.size());return status==1?Verification::Valid:status==0?Verification::Invalid:Verification::Unavailable;
 }catch(...){return Verification::Unavailable;}
}
bool verifySignature(std::string_view request,std::string_view signature,RsaPublicKey key)noexcept{return verifySignatureStatus(request,signature,key)==Verification::Valid;}
Verification verifyOfficialStatus(std::string_view request,std::string_view signature)noexcept{return verifySignatureStatus(request,signature,officialPublicKey());}
bool verifyOfficial(std::string_view request,std::string_view signature)noexcept{return verifySignature(request,signature,officialPublicKey());}
}
