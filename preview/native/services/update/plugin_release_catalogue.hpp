// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArrayView>
#include <QIODevice>
#include <QString>
#include <QUrl>
#include <expected>
#include <optional>
#include <utility>

namespace biglybtc::update {

inline constexpr qsizetype MaximumReleaseCatalogueBytes = 64 * 1024;

enum class CatalogueError { InvalidInput, Signature, Format, Replay, Expired, Incompatible, Ambiguous };

struct PluginReleaseTarget {
  QString runtimeId;
  QString repository; // Explicitly approved GitHub owner/name.
  QString platform;
  QString qtVersion;
  qint64 hostApi;
  qint64 hostAbi;
  bool operator==(const PluginReleaseTarget&) const = default;
};

struct PluginReleaseAsset {
  QString version;
  QString filename;
  QUrl url;
  qint64 bytes;
  QByteArray sha256;
  QByteArray nativeCommit;
  QByteArray upstreamCommit;
};

class VerifiedPluginRelease final {
public:
  qint64 sequence() const { return sequence_; }
  qint64 expiresAt() const { return expiresAt_; }
  const PluginReleaseAsset& asset() const { return asset_; }
  const PluginReleaseTarget& target() const { return target_; }
  const QByteArray& catalogue() const { return catalogue_; }
  const QByteArray& signature() const { return signature_; }
  const QByteArray& signingKey() const { return signingKey_; }

private:
  VerifiedPluginRelease(qint64 sequence, qint64 expires, PluginReleaseTarget target, PluginReleaseAsset asset,
      QByteArray catalogue, QByteArray signature, QByteArray signingKey)
      : sequence_(sequence), expiresAt_(expires), target_(std::move(target)), asset_(std::move(asset)),
        catalogue_(std::move(catalogue)), signature_(std::move(signature)), signingKey_(std::move(signingKey)) {}
  friend std::expected<VerifiedPluginRelease, CatalogueError> verifyPluginReleaseCatalogue(
      QByteArrayView, QByteArrayView, QByteArrayView, const PluginReleaseTarget&, qint64, qint64);

  qint64 sequence_;
  qint64 expiresAt_;
  PluginReleaseTarget target_;
  PluginReleaseAsset asset_;
  QByteArray catalogue_, signature_, signingKey_;
};

// Locate the named catalogue in the approved repository's release channel.
std::optional<QUrl> pluginReleaseCatalogueUrl(const PluginReleaseTarget&, QStringView name);

// Signature covers the exact supplied catalogue bytes. The trust root and
// previous committed sequence belong to the installation owner.
std::expected<VerifiedPluginRelease, CatalogueError> verifyPluginReleaseCatalogue(
    QByteArrayView catalogue, QByteArrayView signature, QByteArrayView ed25519PublicKey,
    const PluginReleaseTarget&, qint64 lastSequence, qint64 now);

// Verify a complete staged artifact from offset zero through EOF.
bool verifyPluginReleaseAsset(QIODevice&, const PluginReleaseAsset&);

} // namespace biglybtc::update
