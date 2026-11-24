// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_catalogue.hpp"
#include "plugin_release_test_signing.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <archive.h>
#include <archive_entry.h>
#include <initializer_list>

namespace biglybtc::update::test {
struct File {
  const char* path;
  QByteArray data;
  const char* symlink = nullptr;
};

inline QByteArray archiveFixture(std::initializer_list<File> files, bool compressed = true) {
  const std::unique_ptr<archive, decltype(&archive_write_free)> output(archive_write_new(), archive_write_free);
  require(output && archive_write_set_format_pax_restricted(output.get()) == ARCHIVE_OK &&
      archive_write_set_bytes_per_block(output.get(), 0) == ARCHIVE_OK, "Set the fixture archive format");
  if (compressed) {
    require(archive_write_add_filter_zstd(output.get()) == ARCHIVE_OK, "Use the built-in Zstandard writer in the fixture");
  }
  QByteArray bytes(2 * 1024 * 1024, Qt::Uninitialized);
  std::size_t written = 0;
  require(archive_write_open_memory(output.get(), bytes.data(), bytes.size(), &written) == ARCHIVE_OK,
      "Open the in-memory fixture archive");
  for (const auto& file : files) {
    const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
    require(bool(entry), "Create the fixture member");
    archive_entry_set_pathname(entry.get(), file.path);
    archive_entry_set_filetype(entry.get(), file.symlink ? AE_IFLNK : AE_IFREG);
    archive_entry_set_perm(entry.get(), 0400);
    archive_entry_set_size(entry.get(), file.data.size());
    if (file.symlink) { archive_entry_set_symlink(entry.get(), file.symlink); }
    require(archive_write_header(output.get(), entry.get()) == ARCHIVE_OK, "Write the fixture member metadata");
    if (!file.data.isEmpty()) {
      require(archive_write_data(output.get(), file.data.constData(), file.data.size()) == file.data.size(),
          "Write fixture member data");
    }
  }
  require(archive_write_close(output.get()) == ARCHIVE_OK && written > 0, "Finish the fixture archive");
  bytes.resize(static_cast<qsizetype>(written));
  return bytes;
}

struct ReleaseFixture {
  const SigningFixture signer;
  const PluginReleaseTarget target{QStringLiteral("azlocprov"), QStringLiteral("d00rsfan/BiglyBTC-plugin-azlocprov"),
      QStringLiteral("linux-amd64"), QStringLiteral("6.12.0"), 1, 1};
  const QString native = QString(40, u'4');
  const QString upstream = QString(40, u'5');
  QString version = QStringLiteral("0.1.0");

  QByteArray manifest(const QByteArray& module, QString id = QStringLiteral("azlocprov")) const {
    return QJsonDocument(QJsonObject{{"schema", 1}, {"id", std::move(id)}, {"version", version},
        {"platform", target.platform}, {"hostAbi", target.hostAbi}, {"nativeCommit", native},
        {"upstreamCommit", upstream}, {"moduleBytes", module.size()},
        {"moduleSha256", QString::fromLatin1(QCryptographicHash::hash(module, QCryptographicHash::Sha256).toHex())}})
        .toJson(QJsonDocument::Compact);
  }

  VerifiedPluginRelease select(const QByteArray& package, qint64 sequence = 12) const {
    const QJsonObject entry{{"id", target.runtimeId}, {"version", version}, {"platform", target.platform},
        {"qt", target.qtVersion}, {"minHostApi", 1}, {"maxHostApi", 1}, {"minHostAbi", 1}, {"maxHostAbi", 1},
        {"bytes", package.size()}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex())},
        {"nativeCommit", native}, {"upstreamCommit", upstream},
        {"url", QStringLiteral("https://github.com/d00rsfan/BiglyBTC-plugin-azlocprov/releases/download/v") + version +
            QStringLiteral("/azlocprov-linux-amd64.tar.zst")}};
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto [data, signature] = signedCatalogue(signer, sequence, now + 3600, {entry});
    auto release = verifyPluginReleaseCatalogue(data, signature, signer.publicKey(), target, 11, now);
    require(bool(release), "Verify the signed local fixture catalogue");
    return std::move(*release);
  }
};

} // namespace biglybtc::update::test
