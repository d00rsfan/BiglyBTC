// SPDX-License-Identifier: GPL-2.0-or-later
#include "generate_identity.hpp"
#include <algorithm>
#include <memory>
#include <new>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
namespace biglybtc::services::i2p {
Result<ClientIdentity> generateClientIdentity() {
  struct Secrets {
    std::array<Byte,256> encryption{};
    std::array<Byte,32> signing{};
    ~Secrets(){OPENSSL_cleanse(encryption.data(),encryption.size());OPENSSL_cleanse(signing.data(),signing.size());}
  } secrets;
  const auto failure=[](){return std::unexpected(Error{ErrorCode::CryptoFailure,"Cannot generate original online I2P identity"});};
  try {
    std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_from_name(nullptr,"ED25519",nullptr),EVP_PKEY_CTX_free);
    EVP_PKEY* raw=nullptr;
    if(!context||EVP_PKEY_keygen_init(context.get())!=1){return failure();}
    const int generated=EVP_PKEY_generate(context.get(),&raw);
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(raw,EVP_PKEY_free);
    if(generated!=1||!key){return failure();}
    std::array<Byte,32> publicKey{},pattern{};std::size_t pub=publicKey.size(),priv=secrets.signing.size();
    if(EVP_PKEY_get_raw_public_key(key.get(),publicKey.data(),&pub)!=1||pub!=32
        ||EVP_PKEY_get_raw_private_key(key.get(),secrets.signing.data(),&priv)!=1||priv!=32
        ||RAND_bytes(pattern.data(),static_cast<int>(pattern.size()))!=1
        ||RAND_priv_bytes(secrets.encryption.data(),static_cast<int>(secrets.encryption.size()))!=1){return failure();}
    // I2PClientImpl deliberately uses unrelated random public/private Enc0
    // bytes: they have not been encryption keys since2005. Its32-byte public
    // pattern is repeated over256-byte public field and96-byte signing pad.
    Bytes destination(391);
    for(std::size_t i=0;i<352;++i){destination[i]=pattern[i%pattern.size()];}
    std::copy(publicKey.begin(),publicKey.end(),destination.begin()+352);
    destination[384]=5;destination[385]=0;destination[386]=4;
    destination[387]=0;destination[388]=7;destination[389]=0;destination[390]=0;
    return makeClientIdentity(destination,secrets.encryption,secrets.signing);
  } catch(const std::bad_alloc&) {return failure();}
}
}
