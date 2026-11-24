// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_catalogue.hpp"
#include "plugin_release_stage.hpp"
#include "plugin_release_test_signing.hpp"

#include <QBuffer>
#include <archive.h>
#include <archive_entry.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <sys/stat.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace biglybtc::update;
using namespace biglybtc::update::test;

namespace {
void write(const QString& path, QByteArrayView bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly) && file.write(bytes.data(), bytes.size()) == bytes.size(), "Write an owned fixture");
}

QByteArray read(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Read an owned fixture");
  return file.readAll();
}

QByteArray javaJar(std::initializer_list<std::pair<const char*, QByteArray>> entries) {
  using Writer = std::unique_ptr<archive, decltype(&archive_write_free)>;
  Writer writer(archive_write_new(), archive_write_free);
  QByteArray result(128 * 1024, '\0');
  std::size_t length = 0;
  require(writer && archive_write_set_format_zip(writer.get()) == ARCHIVE_OK &&
      archive_write_open_memory(writer.get(), result.data(), result.size(), &length) == ARCHIVE_OK,
      "Create a generated ZIP-format Java JAR");
  for (const auto& [name, bytes] : entries) {
    const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
    require(bool(entry), "Generate a Java resource entry");
    archive_entry_set_pathname(entry.get(), name);
    archive_entry_set_size(entry.get(), bytes.size());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    require(archive_write_header(writer.get(), entry.get()) == ARCHIVE_OK &&
        archive_write_data(writer.get(), bytes.data(), bytes.size()) == bytes.size(), "Write the generated Java resource");
  }
  require(archive_write_close(writer.get()) == ARCHIVE_OK, "Finish the generated JAR directory");
  result.truncate(length);
  return result;
}

void stage(const VerifiedPluginRelease& selection, const VerifiedPluginRelease& expired, QByteArray input) {
  using biglybtc::storage::ErrorCode;
  QTemporaryDir area;
  require(area.isValid(), "Create a generated profile");
  const auto config = area.filePath(QStringLiteral("biglybt.config"));
  const auto original = area.filePath(QStringLiteral("plugins/CustomXmWeb"));
  const auto native = original + QStringLiteral("/biglybtc");
  const auto pending = native + QStringLiteral("/pending/linux-amd64/4/0.1.2/release.asset");
  const auto descriptor = original + QStringLiteral("/plugin.properties");
  const auto jar = original + QStringLiteral("/xmwebui_1.0.jar");
  const auto other = area.filePath(QStringLiteral("unrelated"));
  const QByteArray descriptorBytes("# Original exact bytes\r\nplugin\\.id : xmwe\\\r\n  bui\r\nplugin.version=1.0\n");
  const QByteArray jarBytes("Opaque Java artifact remains byte-identical");
  write(config, "d11:opaque-fact3:yes" "e");
  write(other, "external test sentinel");
  auto acquired = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
  require(bool(acquired), "Acquire the same retained profile lease as the host");
  std::optional<biglybtc::storage::ProfileLease> lease(std::in_place, std::move(*acquired));
  QBuffer inputDevice(&input);
  require(inputDevice.open(QIODevice::ReadOnly), "Open the signed fixture input");
  const auto apply = [&](const VerifiedPluginRelease& release) {
    return stagePluginRelease(*lease, "CustomXmWeb", release, inputDevice);
  };
  const auto missing = apply(selection);
  require(!missing && missing.error().code == ErrorCode::Missing && !QFileInfo::exists(original),
      "A missing Java directory is never created from signed catalogue or saved config");

  require(QDir().mkpath(original), "Install a generated Java directory with its real basename");
  write(descriptor, descriptorBytes);
  write(jar, jarBytes);
  const auto wrongDirectory = stagePluginRelease(*lease, "../outside", selection, inputDevice);
  require(!wrongDirectory && wrongDirectory.error().code == ErrorCode::UnsafePath, "Require a single original directory basename");
  const auto late = apply(expired);
  require(!late && late.error().code == ErrorCode::Unsupported, "Refuse a previously verified release after expiry");

  write(config, "d26:PluginInfo.xmwebui.enabledi0e11:opaque-fact3:yese");
  const auto disabled = apply(selection);
  require(!disabled && disabled.error().code == ErrorCode::Unsupported && !QFileInfo::exists(native),
      "The original disabled state refuses automatic staging");
  write(config, "d11:opaque-fact3:yes" "e");
  write(descriptor, descriptorBytes + QByteArray("plugin.id=other\n"));
  const auto ambiguous = apply(selection);
  require(!ambiguous && ambiguous.error().code == ErrorCode::Conflict && !QFileInfo::exists(native),
      "Reject an ambiguous Java runtime ID");
  write(descriptor, descriptorBytes);
  const auto bytes = input;
  input[7777] = 'x';
  const auto corrupted = apply(selection);
  require(!corrupted && corrupted.error().code == ErrorCode::InvalidFormat && !QFileInfo::exists(native),
      "Unverified bytes gain no persistent native pathname");
  input = bytes;
  require(QFile::link(other, native), "Generate a foreign native subtree link");
  const auto aliased = apply(selection);
  require(!aliased && aliased.error().code == ErrorCode::UnsafePath && QFileInfo(native).isSymLink() &&
      read(other) == "external test sentinel", "Do not follow or replace a native subtree link");
  require(QFile::remove(native), "Remove only the generated link");

  const auto published = apply(selection);
  require(published && *published == PluginReleaseStage::Published && read(pending) == bytes &&
      !QFileInfo::exists(native + QStringLiteral("/installed.json")), "Persist signed bytes in a pending version slot");
  struct stat before{}, after{};
  require(::stat(pending.toUtf8().constData(), &before) == 0 && (before.st_mode & 0777) == 0400,
      "The single published bundle has read-only mode");
  const auto proof = QFileInfo(pending).absolutePath() + QStringLiteral("/proof-") +
      QString::number(selection.sequence()) + QStringLiteral(".signed");
  const auto proofBytes = read(proof);
  require(QFile::remove(proof), "Simulate interruption after publishing only the pending asset");
  const auto missingProof = readStagedPluginRelease(*lease, "CustomXmWeb", selection.target(),
      selection.asset().version, selection.sequence(), selection.signingKey(), selection.sequence() - 1);
  require(!missingProof, "An interrupted pending asset cannot authorize offline installation");
  const auto retry = apply(selection);
  require(read(proof) == proofBytes, "Restore the signed proof without replacing the already published asset");
  require(retry && *retry == PluginReleaseStage::AlreadyPresent &&
      ::stat(pending.toUtf8().constData(), &after) == 0 && before.st_ino == after.st_ino,
      "Retry preserves the published inode");
  lease.reset();
  auto reopened = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
  require(bool(reopened), "Reopen the original profile from a fresh owner");
  lease.emplace(std::move(*reopened));
  const auto readProof = [&](QByteArrayView approved, qint64 floor) {
    return readStagedPluginRelease(*lease, "CustomXmWeb", selection.target(), selection.asset().version,
        selection.sequence(), approved, floor);
  };
  const auto candidate = readProof(selection.signingKey(), selection.sequence() - 1);
  require(candidate && candidate->catalogue() == selection.catalogue() &&
      candidate->signature() == selection.signature() && candidate->asset().sha256 == selection.asset().sha256,
      "Authenticate the exact staged release after reopening the profile");
  const SigningFixture foreign;
  require(!readProof(foreign.publicKey(), selection.sequence() - 1) &&
      !readProof(selection.signingKey(), selection.sequence()), "Refuse a foreign key and an already accepted sequence");
  require(::chmod(proof.toUtf8().constData(), 0600) == 0, "Make only the generated proof writable");
  auto altered = proofBytes;
  altered[altered.size() - 1] ^= 1;
  write(proof, altered);
  require(::chmod(proof.toUtf8().constData(), 0400) == 0 &&
      !readProof(selection.signingKey(), selection.sequence() - 1), "Reject a tampered proof with the expected inode mode");
  std::cout << "PASS immutable pending proof, asset-only retry, profile reopen, replay and signature checks\n";
  require(::chmod(pending.toUtf8().constData(), 0600) == 0, "Make the generated pending file writable for tamper test");
  write(pending, "retained conflict");
  const auto conflict = apply(selection);
  require(!conflict && conflict.error().code == ErrorCode::Conflict && read(pending) == "retained conflict",
      "Do not replace an existing mismatched version slot");
  require(read(config) == "d11:opaque-fact3:yese" && read(jar) == jarBytes && read(descriptor) == descriptorBytes,
      "Preserve the original descriptor, JAR and configuration bytes");
  require(QDir().rename(original, original + QStringLiteral("-java-uninstalled")), "Simulate a Java uninstall in the fixture");
  const auto uninstalled = apply(selection);
  require(!uninstalled && uninstalled.error().code == ErrorCode::Missing && !QFileInfo::exists(original),
      "A removed Java installation is not restored by the updater");

  require(QDir().mkpath(original), "Own a generated installation with only JAR descriptors");
  const auto oldJar = original + QStringLiteral("/xmwebui_1.2.jar");
  const auto latestJar = original + QStringLiteral("/xmwebui_1.10_cvs.jar");
  const auto extraJar = original + QStringLiteral("/other_1.0.jar");
  const auto latest = javaJar({{"META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n"},
      {"plugin.properties", descriptorBytes}});
  write(oldJar, javaJar({{"plugin.properties", "plugin.id=obsolete\n"}}));
  write(latestJar, latest);
  write(original + QStringLiteral("/unversioned.jar"), "Original versioned selection drops this file");
  write(original + QStringLiteral("/dependency_2.0.jar"), javaJar({{"fixture/class.dat", "no root descriptor"}}));
  const auto jarOnly = apply(selection);
  require(jarOnly && *jarOnly == PluginReleaseStage::Published && read(latestJar) == latest && read(pending) == bytes,
      "Select the original highest version, read its root JAR descriptor, and preserve the actual Java archive");
  const auto jarCandidate = readStagedPluginRelease(*lease, "CustomXmWeb", selection.target(),
      selection.asset().version, selection.sequence(), selection.signingKey(), selection.sequence() - 1);
  require(bool(jarCandidate), "Reopen the signed staged update against the same JAR-only installation");
  write(extraJar, javaJar({{"plugin.properties", descriptorBytes}}));
  const auto duplicate = apply(selection);
  require(!duplicate && duplicate.error().code == ErrorCode::Conflict, "Reject ambiguous selected JAR descriptors");
  require(QFile::remove(extraJar), "Retire only the generated ambiguous descriptor fixture");
  write(descriptor, "plugin.id=other\n");
  const auto loose = apply(selection);
  require(!loose && loose.error().code == ErrorCode::Conflict, "Honor the original loose descriptor override");
  require(QFile::remove(descriptor), "Retire only the generated loose override");
  write(latestJar, javaJar({{"plugin.properties", descriptorBytes}, {"plugin.properties", descriptorBytes}}));
  const auto duplicateMember = apply(selection);
  require(!duplicateMember, "Refuse duplicate root descriptor entries in one Java archive");
  std::cout << "PASS original highest-version JAR descriptor, pending proof reopen and loose override\n";

  const QByteArray withoutId("plugin.class=generated.TestPlugin\nplugin.version=1.10\n");
  const auto withoutIdJar = javaJar({{"plugin.properties", withoutId}});
  write(latestJar, withoutIdJar);
  const auto uncertain = apply(selection);
  require(!uncertain && uncertain.error().code == ErrorCode::Conflict,
      "An implicit ID with multiple versioned prefixes is ambiguous");
  require(QFile::remove(original + QStringLiteral("/dependency_2.0.jar")), "Retire the generated alternate prefix");
  const auto derived = apply(selection);
  require(derived && *derived == PluginReleaseStage::AlreadyPresent && read(latestJar) == withoutIdJar,
      "Use the only selected versioned prefix for a JAR descriptor with no explicit ID");
  const auto derivedProof = readStagedPluginRelease(*lease, "CustomXmWeb", selection.target(),
      selection.asset().version, selection.sequence(), selection.signingKey(), selection.sequence() - 1);
  require(bool(derivedProof), "Reopen a signed update with the exact derived original ID");
  write(descriptor, withoutId);
  require(bool(apply(selection)), "A loose descriptor uses the same versioned prefix when its ID is absent");
  write(descriptor, "plugin.id=\n");
  const auto explicitEmpty = apply(selection);
  require(!explicitEmpty && explicitEmpty.error().code == ErrorCode::Conflict,
      "An explicitly empty ID does not fall back to the selected prefix");

  const auto unversionedFolder = area.filePath(QStringLiteral("plugins/xmwebui"));
  require(QDir().mkpath(unversionedFolder), "Create a generated plugin whose ID comes from its original directory");
  const auto uppercaseJar = unversionedFolder + QStringLiteral("/unversioned.JAR");
  write(uppercaseJar, withoutIdJar);
  const auto uppercase = stagePluginRelease(*lease, "xmwebui", selection, inputDevice);
  require(!uppercase && uppercase.error().code == ErrorCode::Missing,
      "The original classloader does not add an uppercase JAR suffix");
  const auto lowerJar = unversionedFolder + QStringLiteral("/unversioned.jar");
  require(QFile::rename(uppercaseJar, lowerJar), "Use the original lowercase classloader suffix");
  const auto fromDirectory = stagePluginRelease(*lease, "xmwebui", selection, inputDevice);
  require(fromDirectory && *fromDirectory == PluginReleaseStage::Published && read(lowerJar) == withoutIdJar,
      "An original unversioned installation uses the directory basename");
  std::cout << "PASS unambiguous original plugin runtime ID from versioned JAR prefix or directory\n";
}
} // namespace

int main() try {
  using Error = CatalogueError;
  const SigningFixture signer;
  const SigningFixture unrelated;
  const auto key = signer.publicKey();
  const qint64 time = QDateTime::currentSecsSinceEpoch();
  const PluginReleaseTarget target{QStringLiteral("xmwebui"), QStringLiteral("d00rsfan/BiglyBTC-plugin-xmwebui"),
      QStringLiteral("linux-amd64"), QStringLiteral("6.12.0"), 2, 4};
  QByteArray artifact(160 * 1024, '\x16');
  const auto digest = QCryptographicHash::hash(artifact, QCryptographicHash::Sha256);
  const QJsonObject entry{
      {"id", "xmwebui"}, {"version", "0.1.2"}, {"platform", "linux-amd64"}, {"qt", "6.12.0"},
      {"minHostApi", 1}, {"maxHostApi", 3}, {"minHostAbi", 4}, {"maxHostAbi", 4},
      {"bytes", static_cast<qint64>(artifact.size())}, {"sha256", QString::fromLatin1(digest.toHex())},
      {"nativeCommit", QString(40, u'4')}, {"upstreamCommit", QString(40, u'5')},
      {"url", "https://github.com/d00rsfan/BiglyBTC-plugin-xmwebui/releases/download/v0.1.2/xmwebui-linux-amd64.tar.zst"},
  };
  auto otherPlatform = entry;
  otherPlatform.insert("platform", "linux-arm64");
  const auto [payload, signature] = signedCatalogue(signer, 12, time + 3600, {otherPlatform, entry});
  const auto selected = verifyPluginReleaseCatalogue(payload, signature, key, target, 11, time);
  require(selected && selected->sequence() == 12 && selected->expiresAt() == time + 3600 && selected->asset().sha256 == digest &&
      selected->asset().filename == u"xmwebui-linux-amd64.tar.zst", "Choose the exact compatible signed release");

  const auto wrongSignature = verifyPluginReleaseCatalogue(payload + '\n', signature, key, target, 11, time);
  const auto wrongKey = verifyPluginReleaseCatalogue(payload, signature, unrelated.publicKey(), target, 11, time);
  const auto replay = verifyPluginReleaseCatalogue(payload, signature, key, target, 12, time);
  const auto expired = verifyPluginReleaseCatalogue(payload, signature, key, target, 11, time + 3600);
  require(!wrongSignature && wrongSignature.error() == Error::Signature && !wrongKey && wrongKey.error() == Error::Signature &&
      !replay && replay.error() == Error::Replay && !expired && expired.error() == Error::Expired,
      "Verify exact signed bytes, trust root, monotonic sequence and expiry");
  auto mismatch = target;
  mismatch.hostAbi++;
  const auto incompatible = verifyPluginReleaseCatalogue(payload, signature, key, mismatch, 11, time);
  require(!incompatible && incompatible.error() == Error::Incompatible, "Refuse a different host ABI");
  mismatch = target;
  mismatch.qtVersion = QStringLiteral("6.10.2");
  const auto wrongQt = verifyPluginReleaseCatalogue(payload, signature, key, mismatch, 11, time);
  require(!wrongQt && wrongQt.error() == Error::Incompatible, "Refuse an unqualified runtime version");
  const auto [duplicates, duplicatesSignature] = signedCatalogue(signer, 12, time + 3600, {entry, entry});
  const auto ambiguous = verifyPluginReleaseCatalogue(duplicates, duplicatesSignature, key, target, 11, time);
  require(!ambiguous && ambiguous.error() == Error::Ambiguous, "Reject conflicting compatible entries");
  for (const auto* url : {"https://example.test/d00rsfan/BiglyBTC-plugin-xmwebui/releases/download/v0.1.2/file",
          "https://github.com/different/BiglyBTC-plugin-xmwebui/releases/download/v0.1.2/file",
          "https://github.com/d00rsfan/BiglyBTC-plugin-xmwebui/releases/latest/download/file",
          "https://github.com/d00rsfan/BiglyBTC-plugin-xmwebui/releases/download/v0.1.2/%2e%2e%2ffile"}) {
    auto entryCopy = entry;
    entryCopy.insert("url", QString::fromLatin1(url));
    const auto [foreign, foreignSignature] = signedCatalogue(signer, 12, time + 3600, {entryCopy});
    const auto rejected = verifyPluginReleaseCatalogue(foreign, foreignSignature, key, target, 11, time);
    require(!rejected && rejected.error() == Error::Format, "Require a versioned asset in the approved repository");
  }

  QBuffer staged(&artifact);
  require(staged.open(QIODevice::ReadOnly) && verifyPluginReleaseAsset(staged, selected->asset()), "Verify exact staged bytes across chunks");
  const auto [oldCatalogue, oldSig] = signedCatalogue(signer, 12, time - 1, {entry});
  const auto admittedEarlier = verifyPluginReleaseCatalogue(oldCatalogue, oldSig, key, target, 11, time - 2);
  require(bool(admittedEarlier), "Create an admitted release that expired before staging");
  stage(*selected, *admittedEarlier, artifact);
  artifact[100000] = '\x55';
  require(!verifyPluginReleaseAsset(staged, selected->asset()), "Reject a changed artifact of the same length");
  artifact.append('x');
  require(!verifyPluginReleaseAsset(staged, selected->asset()), "Reject a trailing artifact byte");
  std::cout << "PASS signed update catalogue, compatibility, replay and staged asset identity and durable pending publication\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
