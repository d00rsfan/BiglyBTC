#include "crypto.hpp"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/param_build.h>

#include <memory>
#include <stdexcept>

namespace biglybtc::probe {
namespace {

using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using KeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using Digest = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using Builder = std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)>;
using Parameters = std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)>;
using BigNumber = std::unique_ptr<BIGNUM, decltype(&BN_clear_free)>;

void require(bool condition) {
  if (!condition) {
    const auto error = ERR_get_error();
    char description[256]{};
    ERR_error_string_n(error, description, sizeof(description));
    throw std::runtime_error(std::string("Legacy EC compatibility operation failed: ") + description);
  }
}

Key importKey(std::string_view publicKey, std::string_view privateKey = {}) {
  // The vendored Java point preserves compression even for getEncoded(false).
  // Accept the actual 25-byte legacy form and the standard uncompressed form.
  require((publicKey.size() == 25 && (publicKey.front() == '\x02' || publicKey.front() == '\x03'))
      || (publicKey.size() == 49 && publicKey.front() == '\x04'));
  Builder builder(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
  require(static_cast<bool>(builder));
  require(OSSL_PARAM_BLD_push_utf8_string(builder.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime192v2", 0) == 1);
  require(OSSL_PARAM_BLD_push_octet_string(builder.get(), OSSL_PKEY_PARAM_PUB_KEY,
      publicKey.data(), publicKey.size()) == 1);
  BigNumber number(nullptr, BN_clear_free);
  if (!privateKey.empty()) {
    require(privateKey.size() <= 25 && (static_cast<unsigned char>(privateKey.front()) & 0x80) == 0);
    number.reset(BN_bin2bn(reinterpret_cast<const unsigned char*>(privateKey.data()),
        static_cast<int>(privateKey.size()), nullptr));
    require(number && !BN_is_zero(number.get()));
    require(OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_PRIV_KEY, number.get()) == 1);
  }
  Parameters parameters(OSSL_PARAM_BLD_to_param(builder.get()), OSSL_PARAM_free);
  KeyContext context(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
  require(parameters && context);
  require(EVP_PKEY_fromdata_init(context.get()) == 1);
  EVP_PKEY* raw = nullptr;
  const auto selection = privateKey.empty() ? EVP_PKEY_PUBLIC_KEY : EVP_PKEY_KEYPAIR;
  const int imported = EVP_PKEY_fromdata(context.get(), &raw, selection, parameters.get());
  Key key(raw, EVP_PKEY_free);
  require(imported == 1 && key);
  KeyContext check(EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr), EVP_PKEY_CTX_free);
  require(check && EVP_PKEY_public_check(check.get()) == 1);
  if (!privateKey.empty()) {
    require(EVP_PKEY_private_check(check.get()) == 1 && EVP_PKEY_pairwise_check(check.get()) == 1);
  }
  return key;
}

} // namespace

bool verifyLegacySignature(std::string_view publicKey, std::string_view message, std::string_view signature) {
  if (message.size() > 4 * 1024 * 1024 || signature.size() > 128 || signature.empty()) {
    return false;
  }
  try {
    const auto key = importKey(publicKey);
    Digest digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(static_cast<bool>(digest));
    require(EVP_DigestVerifyInit_ex(digest.get(), nullptr, "SHA1", nullptr, nullptr, key.get(), nullptr) == 1);
    return EVP_DigestVerify(digest.get(), reinterpret_cast<const unsigned char*>(signature.data()),
        signature.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
  } catch (const std::runtime_error&) {
    return false;
  }
}

std::string signLegacyMessage(std::string_view privateKey, std::string_view publicKey, std::string_view message) {
  require(!privateKey.empty() && message.size() <= 4 * 1024 * 1024);
  const auto key = importKey(publicKey, privateKey);
  Digest digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  require(static_cast<bool>(digest));
  require(EVP_DigestSignInit_ex(digest.get(), nullptr, "SHA1", nullptr, nullptr, key.get(), nullptr) == 1);
  std::size_t size = 0;
  require(EVP_DigestSign(digest.get(), nullptr, &size,
      reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1);
  require(size <= 128);
  std::string signature(size, '\0');
  require(EVP_DigestSign(digest.get(), reinterpret_cast<unsigned char*>(signature.data()), &size,
      reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1);
  signature.resize(size);
  return signature;
}

} // namespace biglybtc::probe
