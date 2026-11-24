// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_catalogue.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <memory>
#include <optional>
#include <openssl/evp.h>

namespace biglybtc::update {
namespace {
constexpr qsizetype maximumReleases = 128;

bool token(QStringView value, qsizetype maximum = 128) {
  return !value.isEmpty() && value.size() <= maximum && value != u"." && value != u".." &&
      std::ranges::all_of(value, [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') ||
            c == u'.' || c == u'_' || c == u'-';
      });
}

bool hexadecimal(QByteArrayView bytes, qsizetype size) {
  return bytes.size() == size && std::ranges::all_of(bytes, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool commit(QByteArrayView bytes) {
  return hexadecimal(bytes, 40) || hexadecimal(bytes, 64);
}

bool signedBy(QByteArrayView catalogue, QByteArrayView signature, QByteArrayView key) {
  const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(EVP_PKEY_new_raw_public_key_ex(
      nullptr, "ED25519", nullptr, reinterpret_cast<const unsigned char*>(key.data()), key.size()), EVP_PKEY_free);
  const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> verify(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  return publicKey && verify && EVP_DigestVerifyInit(verify.get(), nullptr, nullptr, nullptr, publicKey.get()) == 1 &&
      EVP_DigestVerify(verify.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
          reinterpret_cast<const unsigned char*>(catalogue.data()), catalogue.size()) == 1;
}

bool validTarget(const PluginReleaseTarget& target) {
  const auto repository = QStringView(target.repository).split(u'/');
  return repository.size() == 2 && token(repository[0]) && token(repository[1]) && token(target.runtimeId) &&
      token(target.platform) && token(target.qtVersion) && target.hostApi > 0 && target.hostAbi > 0;
}

std::optional<PluginReleaseAsset> parseAsset(const QJsonObject& entry, const QString& repository) {
  auto version = entry.value(QStringLiteral("version")).toString();
  const auto digest = entry.value(QStringLiteral("sha256")).toString().toLatin1();
  auto native = entry.value(QStringLiteral("nativeCommit")).toString().toLatin1();
  auto upstream = entry.value(QStringLiteral("upstreamCommit")).toString().toLatin1();
  const auto bytes = entry.value(QStringLiteral("bytes")).toInteger();
  auto url = QUrl(entry.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
  if (!token(version) || !hexadecimal(digest, 64) || !commit(native) || !commit(upstream) || bytes <= 0 ||
      !url.isValid() || url.scheme() != u"https" || url.host() != u"github.com" || url.port() != -1 ||
      url.authority().contains('@') || url.hasQuery() || url.hasFragment()) {
    return std::nullopt;
  }
  const auto prefix = QStringLiteral("/") + repository + QStringLiteral("/releases/download/");
  const auto path = url.path(QUrl::FullyEncoded);
  if (!path.startsWith(prefix)) { return std::nullopt; }
  const auto parts = QStringView(path).sliced(prefix.size()).split(u'/');
  if (parts.size() != 2 || !token(parts[0]) || !token(parts[1]) ||
      (parts[0] != version && parts[0] != QStringLiteral("v") + version)) {
    return std::nullopt;
  }
  return PluginReleaseAsset{std::move(version), parts[1].toString(), std::move(url), bytes,
      QByteArray::fromHex(digest), std::move(native), std::move(upstream)};
}
} // namespace

std::optional<QUrl> pluginReleaseCatalogueUrl(const PluginReleaseTarget& target, QStringView name) {
  if (!validTarget(target) || !token(name)) { return std::nullopt; }
  return QUrl(QStringLiteral("https://github.com/") + target.repository + QStringLiteral("/releases/latest/download/")
      + name, QUrl::StrictMode);
}

std::expected<VerifiedPluginRelease, CatalogueError> verifyPluginReleaseCatalogue(
    QByteArrayView catalogue, QByteArrayView signature, QByteArrayView publicKey,
    const PluginReleaseTarget& target, qint64 lastSequence, qint64 now) {
  using Error = CatalogueError;
  if (catalogue.empty() || catalogue.size() > MaximumReleaseCatalogueBytes || signature.size() != 64 || publicKey.size() != 32 ||
      !validTarget(target) || lastSequence < 0 || now <= 0) {
    return std::unexpected(Error::InvalidInput);
  }
  if (!signedBy(catalogue, signature, publicKey)) { return std::unexpected(Error::Signature); }
  const auto document = QJsonDocument::fromJson(catalogue.toByteArray());
  if (!document.isObject()) { return std::unexpected(Error::Format); }
  const auto root = document.object();
  const auto sequence = root.value(QStringLiteral("sequence")).toInteger();
  const auto expires = root.value(QStringLiteral("expiresAt")).toInteger();
  const auto releases = root.value(QStringLiteral("releases"));
  if (root.value(QStringLiteral("schema")).toInteger() != 1 || sequence <= 0 || expires <= 0 ||
      !releases.isArray() || releases.toArray().size() > maximumReleases) {
    return std::unexpected(Error::Format);
  }
  if (sequence <= lastSequence) { return std::unexpected(Error::Replay); }
  if (expires <= now) { return std::unexpected(Error::Expired); }
  std::optional<PluginReleaseAsset> selected;
  for (const auto& value : releases.toArray()) {
    if (!value.isObject()) { return std::unexpected(Error::Format); }
    const auto entry = value.toObject();
    if (entry.value(QStringLiteral("id")).toString() != target.runtimeId ||
        entry.value(QStringLiteral("platform")).toString() != target.platform ||
        entry.value(QStringLiteral("qt")).toString() != target.qtVersion) {
      continue;
    }
    const auto minimumApi = entry.value(QStringLiteral("minHostApi")).toInteger();
    const auto maximumApi = entry.value(QStringLiteral("maxHostApi")).toInteger();
    const auto minimumAbi = entry.value(QStringLiteral("minHostAbi")).toInteger();
    const auto maximumAbi = entry.value(QStringLiteral("maxHostAbi")).toInteger();
    if (minimumApi <= 0 || maximumApi < minimumApi || minimumAbi <= 0 || maximumAbi < minimumAbi) {
      return std::unexpected(Error::Format);
    }
    if (target.hostApi < minimumApi || target.hostApi > maximumApi || target.hostAbi < minimumAbi || target.hostAbi > maximumAbi) {
      continue;
    }
    if (selected) { return std::unexpected(Error::Ambiguous); }
    selected = parseAsset(entry, target.repository);
    if (!selected) { return std::unexpected(Error::Format); }
  }
  if (!selected) { return std::unexpected(Error::Incompatible); }
  return VerifiedPluginRelease{sequence, expires, target, std::move(*selected),
      catalogue.toByteArray(), signature.toByteArray(), publicKey.toByteArray()};
}

bool verifyPluginReleaseAsset(QIODevice& source, const PluginReleaseAsset& asset) {
  if (asset.bytes <= 0 || asset.sha256.size() != 32 || !source.isReadable() || source.isSequential() ||
      source.size() != asset.bytes || !source.seek(0)) {
    return false;
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  for (qint64 remaining = asset.bytes; remaining > 0;) {
    const auto bytes = source.read(std::min<qint64>(remaining, 64 * 1024));
    if (bytes.isEmpty() || bytes.size() > remaining) { return false; }
    digest.addData(bytes);
    remaining -= bytes.size();
  }
  return source.atEnd() && source.size() == asset.bytes && digest.result() == asset.sha256;
}

} // namespace biglybtc::update
