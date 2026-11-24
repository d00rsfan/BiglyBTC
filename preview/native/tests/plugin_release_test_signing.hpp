// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>

namespace biglybtc::update::test {

inline void require(bool value, const char* message) {
  if (!value) { throw std::runtime_error(message); }
}

class SigningFixture final {
public:
  SigningFixture() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"), EVP_PKEY_free) {
    require(bool(key_), "Create an ephemeral signing fixture");
  }

  QByteArray publicKey() const {
    QByteArray data(32, '\0');
    std::size_t size = static_cast<std::size_t>(data.size());
    require(EVP_PKEY_get_raw_public_key(key_.get(), reinterpret_cast<unsigned char*>(data.data()), &size) == 1 && size == 32,
        "Read the fixture's public key");
    return data;
  }

  QByteArray sign(QByteArrayView message) const {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1, "Initialize the fixture signature");
    QByteArray result(64, '\0');
    std::size_t size = static_cast<std::size_t>(result.size());
    require(EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(result.data()), &size,
        reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1 && size == 64, "Sign the fixture bytes");
    return result;
  }

private:
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key_;
};

inline auto signedCatalogue(const SigningFixture& signer, qint64 sequence, qint64 expires, const QJsonArray& releases) {
  const auto payload = QJsonDocument(QJsonObject{{"schema", 1}, {"sequence", sequence},
      {"expiresAt", expires}, {"releases", releases}}).toJson(QJsonDocument::Compact);
  return std::pair(payload, signer.sign(payload));
}

} // namespace biglybtc::update::test
