// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_bundle.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <optional>

namespace biglybtc::update {
namespace {
constexpr qint64 maximumModule = 64 * 1024 * 1024;
constexpr qint64 maximumManifest = 16 * 1024;
constexpr qint64 maximumBundle = maximumModule + 1024 * 1024;

bool sharedObject(QByteArrayView module) {
  // ELF64 file offsets are independent of the host loader and its headers.
  constexpr qsizetype headerBytes = 64;
  constexpr qsizetype objectType = 16, machine = 18, version = 20, headerSize = 52;
  constexpr quint16 shared = 3, x86_64 = 62;
  constexpr quint32 current = 1;
  constexpr QByteArrayView ident("\177ELF\2\1\1", 7); // ELF64, little-endian, version 1.
  if (module.size() < headerBytes || module.first(ident.size()) != ident) { return false; }
  return qFromLittleEndian<quint16>(module.data() + objectType) == shared
      && qFromLittleEndian<quint16>(module.data() + machine) == x86_64
      && qFromLittleEndian<quint32>(module.data() + version) == current
      && qFromLittleEndian<quint16>(module.data() + headerSize) == headerBytes;
}

std::optional<QByteArray> member(archive& reader, archive_entry& entry, qint64 maximum) {
  const auto size = archive_entry_size(&entry);
  if (!archive_entry_size_is_set(&entry) || size <= 0 || size > maximum) { return std::nullopt; }
  QByteArray data(size, Qt::Uninitialized);
  qsizetype position = 0;
  while (position < data.size()) {
    const auto count = archive_read_data(&reader, data.data() + position, data.size() - position);
    if (count <= 0 || count > data.size() - position) { return std::nullopt; }
    position += count;
  }
  char extra;
  if (archive_read_data(&reader, &extra, 1) != 0) { return std::nullopt; }
  return data;
}

bool matchingManifest(const QByteArray& descriptor, const QByteArray& module, const VerifiedPluginRelease& release) {
  const auto manifest = QJsonDocument::fromJson(descriptor).object();
  const auto& target = release.target();
  const auto& asset = release.asset();
  return manifest.size() == 9 && manifest.value(QStringLiteral("schema")).toInteger() == 1 &&
      manifest.value(QStringLiteral("id")).toString() == target.runtimeId &&
      manifest.value(QStringLiteral("version")).toString() == asset.version &&
      manifest.value(QStringLiteral("platform")).toString() == target.platform &&
      manifest.value(QStringLiteral("hostAbi")).toInteger() == target.hostAbi &&
      manifest.value(QStringLiteral("nativeCommit")).toString().toLatin1() == asset.nativeCommit &&
      manifest.value(QStringLiteral("upstreamCommit")).toString().toLatin1() == asset.upstreamCommit &&
      manifest.value(QStringLiteral("moduleBytes")).toInteger() == module.size() &&
      manifest.value(QStringLiteral("moduleSha256")).toString().toLatin1() ==
          QCryptographicHash::hash(module, QCryptographicHash::Sha256).toHex();
}
} // namespace

std::expected<VerifiedPluginBundle, PluginBundleError> readPluginReleaseBundle(
    QIODevice& source, const VerifiedPluginRelease& release) {
  using Error = PluginBundleError;
  const auto& asset = release.asset();
  if (release.target().platform != u"linux-amd64" || !asset.filename.endsWith(u".tar.zst")) {
    return std::unexpected(Error::Format);
  }
  if (asset.bytes <= 0 || asset.bytes > maximumBundle || !source.isReadable() || source.isSequential() ||
      source.size() != asset.bytes || !source.seek(0)) {
    return std::unexpected(Error::AssetIdentity);
  }
  // The parser consumes this exact bounded capture after its digest passes.
  auto captured = source.read(asset.bytes + 1);
  QBuffer verified(&captured);
  if (!source.atEnd() || !verified.open(QIODevice::ReadOnly) || !verifyPluginReleaseAsset(verified, asset)) {
    return std::unexpected(Error::AssetIdentity);
  }
  const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
  if (!reader || archive_read_support_filter_zstd(reader.get()) != ARCHIVE_OK ||
      archive_read_support_format_tar(reader.get()) != ARCHIVE_OK ||
      archive_read_open_memory(reader.get(), captured.constData(), static_cast<std::size_t>(captured.size())) != ARCHIVE_OK ||
      archive_filter_count(reader.get()) != 2 || archive_filter_code(reader.get(), 0) != ARCHIVE_FILTER_ZSTD) {
    return std::unexpected(Error::Format);
  }
  QByteArray manifest, module;
  archive_entry* entry = nullptr;
  int status = archive_read_next_header(reader.get(), &entry);
  while (status == ARCHIVE_OK) {
    const auto* name = archive_entry_pathname(entry);
    if (!name || archive_entry_filetype(entry) != AE_IFREG || archive_entry_hardlink(entry) ||
        archive_entry_symlink(entry) || archive_entry_sparse_count(entry) != 0) {
      return std::unexpected(Error::UnexpectedEntry);
    }
    const QByteArrayView path(name);
    if (path == "manifest.json" && manifest.isEmpty()) {
      auto descriptor = member(*reader, *entry, maximumManifest);
      if (!descriptor) { return std::unexpected(Error::Format); }
      manifest = std::move(*descriptor);
    } else if (path == "plugin.so" && module.isEmpty()) {
      auto candidate = member(*reader, *entry, maximumModule);
      if (!candidate) { return std::unexpected(Error::Format); }
      module = std::move(*candidate);
    } else {
      return std::unexpected(Error::UnexpectedEntry);
    }
    status = archive_read_next_header(reader.get(), &entry);
  }
  if (status != ARCHIVE_EOF || archive_read_close(reader.get()) != ARCHIVE_OK || manifest.isEmpty() ||
      module.isEmpty()) {
    return std::unexpected(Error::Format);
  }
  if (!sharedObject(module) || !matchingManifest(manifest, module, release)) {
    return std::unexpected(Error::ModuleIdentity);
  }
  return VerifiedPluginBundle{std::move(module), std::move(manifest), release};
}

} // namespace biglybtc::update
