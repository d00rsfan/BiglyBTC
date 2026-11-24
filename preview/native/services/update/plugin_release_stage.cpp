// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_stage.hpp"

#include "java_properties.hpp"
#include "original_plugin_jar.hpp"
#include "plugin_release_disk.hpp"

#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace biglybtc::update {
namespace {
using storage::Error;
using storage::ErrorCode;
using storage::detail::Descriptor;
constexpr QByteArrayView proofTag("BiglyBTC native release proof v1\n");
constexpr qsizetype signatureBytes = 64;
constexpr qsizetype maximumProof = proofTag.size() + signatureBytes + MaximumReleaseCatalogueBytes;

std::string proofName(qint64 sequence) {
  return "proof-" + std::to_string(sequence) + ".signed";
}

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

std::optional<qint64> proofSequence(std::string_view name) {
  constexpr std::string_view prefix = "proof-", suffix = ".signed";
  if (name.size() <= prefix.size() + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix)) {
    return std::nullopt;
  }
  const auto decimal = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
  if (decimal.front() == '0') { return std::nullopt; }
  qint64 sequence = 0;
  const auto parsed = std::from_chars(decimal.data(), decimal.data() + decimal.size(), sequence);
  if (parsed.ec != std::errc{} || parsed.ptr != decimal.data() + decimal.size() || sequence <= 0) {
    return std::nullopt;
  }
  return sequence;
}

struct OriginalDescriptor {
  QString runtimeId;
  std::string receipt;
};

std::expected<OriginalDescriptor, Error> originalDescriptor(int parent, std::string_view directoryName) {
  auto loose = storage::detail::readFile(parent, "plugin.properties", 65536);
  if (!loose) { return std::unexpected(loose.error()); }
  std::string properties, identity;
  std::optional<std::string> fallbackId;
  if (*loose) {
    properties = std::move(**loose);
    identity = "loose";
  } else {
    auto jar = detail::readOriginalJarDescriptor(parent, directoryName);
    if (!jar) { return std::unexpected(jar.error()); }
    if (!*jar) { return error(ErrorCode::Missing, "Installed Java plugin descriptor is missing"); }
    properties = std::move((**jar).properties);
    identity = std::move((**jar).identity);
    fallbackId = std::move((**jar).fallbackId);
  }
  const auto parsed = services::readJavaProperties(QString::fromLatin1(properties), 4096,
      services::JavaPropertiesDuplicates::Reject);
  if (!parsed) { return error(ErrorCode::Conflict, "Installed Java plugin descriptor has invalid or duplicate properties"); }

  const auto id = parsed->constFind(QStringLiteral("plugin.id"));
  if (id == parsed->cend() && *loose) {
    auto derived = detail::originalPluginIdFallback(parent, directoryName);
    if (!derived) { return std::unexpected(derived.error()); }
    fallbackId = std::move(*derived);
  }
  if (id == parsed->cend() && !fallbackId) {
    return error(ErrorCode::Conflict, "Installed Java plugin descriptor has an ambiguous runtime ID");
  }
  auto runtimeId = id == parsed->cend() ? QString::fromLatin1(*fallbackId) : *id;
  return OriginalDescriptor{std::move(runtimeId), identity + '\0' + properties};
}

std::expected<void, Error> checkStartup(const storage::ProfileLease& lease, storage::OriginalPluginBinding binding) {
  const auto config = storage::ConfigFile::load(lease, "biglybt.config");
  if (!config) { return std::unexpected(config.error()); }
  if (config->recoverySource() != storage::RecoverySource::Primary) {
    return error(ErrorCode::Conflict, "The original startup setting is in recovery");
  }
  storage::OriginalPluginSettings settings(config->document(), std::move(binding));
  if (!settings.loadedAtStartup() || !settings.wrongTypes().empty()) {
    return error(ErrorCode::Unsupported, "The original startup setting does not admit this release");
  }
  return {};
}

std::expected<void, Error> copyAsset(int descriptor, QIODevice& source, const PluginReleaseAsset& asset) {
  QFile copy;
  if (!copy.open(descriptor, QIODevice::ReadWrite | QIODevice::Unbuffered)) {
    return error(ErrorCode::Io, "Could not open the anonymous pending release artifact");
  }
  if (!source.isReadable() || source.isSequential() || source.size() != asset.bytes || !source.seek(0)) {
    return error(ErrorCode::InvalidFormat, "Pending release asset size disagrees with the signed catalogue");
  }
  for (qint64 remaining = asset.bytes; remaining > 0;) {
    const auto bytes = source.read(std::min<qint64>(remaining, 64 * 1024));
    if (bytes.isEmpty() || bytes.size() > remaining) {
      return error(ErrorCode::InvalidFormat, "Pending release asset ended unexpectedly");
    }
    if (copy.write(bytes) != bytes.size()) { return error(ErrorCode::Io, "Could not stage the release asset"); }
    remaining -= bytes.size();
  }
  if (!copy.flush()) { return error(ErrorCode::Io, "Could not flush the staged release asset"); }
  if (!source.atEnd() || source.size() != asset.bytes || !verifyPluginReleaseAsset(copy, asset)) {
    return error(ErrorCode::InvalidFormat, "Pending release asset differs from the signed catalogue");
  }
  if (::fchmod(descriptor, 0400) != 0 || ::fsync(descriptor) != 0) {
    return error(ErrorCode::Io, "Could not synchronize the verified release asset");
  }
  return {};
}

std::expected<void, Error> publishProof(int parent, const VerifiedPluginRelease& release) {
  const auto name = proofName(release.sequence());
  const auto expected = proofTag.toByteArray() + release.signature() + release.catalogue();
  const auto proof = detail::preparePluginReceipt(parent, expected);
  if (!proof) { return std::unexpected(proof.error()); }
  if (detail::linkPluginTemporary(proof->get(), parent, name.c_str()) != 0) {
    if (errno != EEXIST) { return error(ErrorCode::Io, "Cannot publish the staged proof"); }
    const auto existing = detail::openArtifact(parent, name.c_str());
    QFile prior;
    if (!existing || !prior.open(existing->get(), QIODevice::ReadOnly) || prior.size() != expected.size() ||
        prior.read(expected.size() + 1) != expected || !prior.atEnd() || !detail::heldAt(parent, name, existing->get())) {
      return error(ErrorCode::Conflict, "The existing staged proof differs from the signed release");
    }
  }
  if (::fsync(parent) != 0) { return error(ErrorCode::Io, "Cannot durably publish the signed staged proof"); }
  return {};
}

std::expected<PluginReleaseStage, Error> verifyExisting(int parent, const PluginReleaseAsset& asset) {
  const auto existing = detail::openArtifact(parent, detail::pendingAsset);
  QFile view;
  if (!existing || !view.open(existing->get(), QIODevice::ReadOnly) || !verifyPluginReleaseAsset(view, asset) ||
      !detail::heldAt(parent, detail::pendingAsset, existing->get())) {
    return error(ErrorCode::Conflict, "Existing pending release belongs to a different or invalid artifact");
  }
  if (::fsync(parent) != 0) { return error(ErrorCode::Io, "Could not synchronize the pending release"); }
  return PluginReleaseStage::AlreadyPresent;
}
} // namespace

namespace detail {
std::expected<std::vector<std::string>, Error> childNames(int directory, std::size_t limit) {
  const auto fd = ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) { return error(ErrorCode::Io, "Cannot open the owned plugin directory inventory"); }
  const auto close = [](DIR* stream) { ::closedir(stream); };
  const std::unique_ptr<DIR, decltype(close)> entries(::fdopendir(fd), close);
  if (!entries) {
    ::close(fd);
    return error(ErrorCode::Io, "Cannot read the owned plugin directory inventory");
  }
  std::vector<std::string> names;
  for (;;) {
    errno = 0;
    const auto* entry = ::readdir(entries.get());
    if (!entry) {
      if (errno) { return error(ErrorCode::Io, "Cannot finish reading the owned plugin directory inventory"); }
      return names;
    }
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") { continue; }
    if (names.size() == limit) { return error(ErrorCode::InvalidFormat, "The owned plugin directory has too many entries"); }
    names.emplace_back(name);
  }
}


std::expected<Descriptor, Error> preparePluginReceipt(int directory, QByteArrayView bytes) {
  Descriptor receipt(::openat(directory, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
  QFile output;
  if (receipt.get() < 0 || !output.open(receipt.get(), QIODevice::WriteOnly | QIODevice::Unbuffered) ||
      output.write(bytes.data(), bytes.size()) != bytes.size() || !output.flush() ||
      ::fchmod(receipt.get(), 0400) != 0 || ::fsync(receipt.get()) != 0) {
    return error(ErrorCode::Io, "Cannot synchronize the owned plugin receipt");
  }
  return receipt;
}

int linkPluginTemporary(int descriptor, int directory, const char* name) {
  const auto source = "/proc/self/fd/" + std::to_string(descriptor);
  return ::linkat(AT_FDCWD, source.c_str(), directory, name, AT_SYMLINK_FOLLOW);
}

bool heldAt(int parent, const std::string& leaf, int descriptor) {
  struct stat opened{}, named{};
  return ::fstat(descriptor, &opened) == 0 &&
      ::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
      opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
}

std::expected<Descriptor, Error> directory(int parent, const std::string& leaf, bool create, bool privateDirectory) {
  if (create && ::mkdirat(parent, leaf.c_str(), 0700) != 0 && errno != EEXIST) {
    return error(ErrorCode::Io, "Could not create pending release directory");
  }
  Descriptor opened(::openat(parent, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (opened.get() < 0) {
    const auto code = errno == ENOENT ? ErrorCode::Missing :
        (errno == ENOTDIR || errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io);
    return error(code, "Plugin directory cannot be opened safely");
  }
  struct stat info{};
  if (::fstat(opened.get(), &info) != 0 || info.st_uid != ::geteuid() ||
      (privateDirectory && (info.st_mode & 0022) != 0) || !heldAt(parent, leaf, opened.get())) {
    return error(ErrorCode::UnsafePath, "Plugin directory ownership or name changed");
  }
  if (create && ::fsync(parent) != 0) {
    return error(ErrorCode::Io, "Could not synchronize the pending release directory name");
  }
  return opened;
}

std::expected<Descriptor, Error> openArtifact(int parent, const char* name) {
  Descriptor file(::openat(parent, name, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
  struct stat info{};
  if (file.get() < 0 || ::fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_nlink != 1 || info.st_uid != ::geteuid() || (info.st_mode & 0777) != 0400 ||
      !heldAt(parent, name, file.get())) {
    return error(ErrorCode::Conflict, "The native release file is not a single owned immutable artifact");
  }
  return file;
}

std::expected<std::vector<Descriptor>, Error> descend(
    int parent, std::span<const std::string> names, bool create) {
  std::vector<Descriptor> chain;
  chain.reserve(names.size());
  for (const auto& leaf : names) {
    auto child = directory(parent, leaf, create);
    if (!child) { return std::unexpected(child.error()); }
    chain.push_back(std::move(*child));
    parent = chain.back().get();
  }
  return chain;
}

bool isCurrentPath(int parent, std::span<const std::string> path, std::span<const Descriptor> chain) {
  if (path.size() != chain.size()) { return false; }
  for (std::size_t index = 0; index < path.size(); ++index) {
    if (!heldAt(parent, path[index], chain[index].get())) { return false; }
    parent = chain[index].get();
  }
  return true;
}

std::array<std::string, 5> pendingPath(const PluginReleaseTarget& target, QStringView version) {
  return {"biglybtc", "pending", target.platform.toStdString(),
      std::to_string(target.hostAbi), version.toString().toStdString()};
}

std::array<std::string, 5> pendingPath(const VerifiedPluginRelease& release) {
  return pendingPath(release.target(), release.asset().version);
}

std::array<std::string, 3> versionParent(const VerifiedPluginRelease& release) {
  return {"biglybtc", release.target().platform.toStdString(), std::to_string(release.target().hostAbi)};
}

std::expected<InstalledJavaPlugin, Error> openInstalledJavaPlugin(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target) {
  if (target.platform != u"linux-amd64") {
    return error(ErrorCode::Unsupported, "The signed plugin release targets a different platform");
  }
  auto binding = storage::originalPluginBinding(target.runtimeId.toStdString(), std::move(originalDirectory));
  if (!binding) { return error(ErrorCode::UnsafePath, "A valid original plugin directory is required"); }
  auto retained = lease.retain();
  if (!retained) { return std::unexpected(retained.error()); }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  auto plugins = directory(lease.directoryHandle(), "plugins", false, false);
  if (!plugins) { return std::unexpected(plugins.error()); }
  auto original = directory(plugins->get(), binding->directoryName, false, false);
  if (!original) { return std::unexpected(original.error()); }
  auto descriptor = originalDescriptor(original->get(), binding->directoryName);
  if (!descriptor) { return std::unexpected(descriptor.error()); }
  if (descriptor->runtimeId != QString::fromLatin1(binding->runtimeId)) {
    return error(ErrorCode::Conflict, "Installed Java plugin descriptor has a different runtime ID");
  }
  if (auto enabled = checkStartup(lease, *binding); !enabled) { return std::unexpected(enabled.error()); }
  return InstalledJavaPlugin{std::move(*retained), std::move(*binding), std::move(*plugins),
      std::move(*original), std::move(descriptor->receipt)};
}

std::expected<void, Error> ensureCurrent(const InstalledJavaPlugin& plugin) {
  auto latest = originalDescriptor(plugin.original.get(), plugin.binding.directoryName);
  if (!latest || latest->runtimeId != QString::fromLatin1(plugin.binding.runtimeId) ||
      latest->receipt != plugin.descriptor || !plugin.lease->retain() ||
      !heldAt(plugin.lease->directoryHandle(), "plugins", plugin.plugins.get()) ||
      !heldAt(plugin.plugins.get(), plugin.binding.directoryName, plugin.original.get())) {
    return error(ErrorCode::Conflict, "The installed Java plugin or profile lease changed during the update");
  }
  return checkStartup(*plugin.lease, plugin.binding);
}

} // namespace detail

std::expected<std::vector<InstalledJavaPluginDirectory>, Error> installedJavaPluginDirectories(
    const storage::ProfileLease& lease, std::stop_token stop) {
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  auto plugins = detail::directory(lease.directoryHandle(), "plugins", false, false);
  if (!plugins && plugins.error().code == ErrorCode::Missing) { return std::vector<InstalledJavaPluginDirectory>{}; }
  if (!plugins) { return std::unexpected(plugins.error()); }

  const auto names = detail::childNames(plugins->get(), 512);
  if (!names) { return std::unexpected(names.error()); }
  std::vector<InstalledJavaPluginDirectory> entries;
  for (const auto& name : *names) {
    if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Java plugin inventory reading was cancelled"); }
    if (name == "CVS") { continue; }
    struct stat info{};
    if (::fstatat(plugins->get(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
      return error(ErrorCode::Conflict, "The original plugins inventory changed during discovery");
    }
    if (S_ISLNK(info.st_mode)) { return error(ErrorCode::UnsafePath, "The original plugins inventory contains a symbolic link"); }
    if (!S_ISDIR(info.st_mode)) { continue; }
    if (!storage::originalPluginBinding("plugins", name)) {
      return error(ErrorCode::UnsafePath, "An original plugin directory is unsafe");
    }
    auto original = detail::directory(plugins->get(), name, false, false);
    if (!original) { return std::unexpected(original.error()); }
    const auto descriptor = originalDescriptor(original->get(), name);
    if (!descriptor && descriptor.error().code == ErrorCode::Missing) { continue; }
    if (!descriptor) { return std::unexpected(descriptor.error()); }
    if (!detail::heldAt(plugins->get(), name, original->get())) {
      return error(ErrorCode::Conflict, "An original plugin directory changed during discovery");
    }
    entries.push_back({descriptor->runtimeId, name});
  }
  if (!detail::heldAt(lease.directoryHandle(), "plugins", plugins->get())) {
    return error(ErrorCode::Conflict, "The original plugins directory changed during discovery");
  }
  return entries;
}

std::expected<std::optional<std::string>, Error> findInstalledJavaPluginDirectory(
    const storage::ProfileLease& lease, const PluginReleaseTarget& target) {
  if (!storage::originalPluginBinding(target.runtimeId.toStdString(), "plugins")) {
    return error(ErrorCode::InvalidFormat, "A valid original plugin runtime ID is required");
  }
  const auto entries = installedJavaPluginDirectories(lease);
  if (!entries) { return std::unexpected(entries.error()); }
  std::optional<std::string> selected;
  for (const auto& entry : *entries) {
    if (entry.runtimeId != target.runtimeId) { continue; }
    if (selected) { return error(ErrorCode::Conflict, "More than one original directory supplies the plugin runtime ID"); }
    selected = entry.directoryName;
  }
  return selected;
}

std::expected<PluginReleaseStage, Error> stagePluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory,
    const VerifiedPluginRelease& release, QIODevice& artifact) {
  if (release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) {
    return error(ErrorCode::Unsupported, "The verified release expired");
  }
  const auto access = lease.sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  const auto plugin = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), release.target());
  if (!plugin) { return std::unexpected(plugin.error()); }

  const Descriptor temporary(::openat(plugin->original.get(), ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
  if (temporary.get() < 0) { return error(ErrorCode::Io, "Cannot create an anonymous file beside the existing Java plugin"); }
  if (auto copied = copyAsset(temporary.get(), artifact, release.asset()); !copied) { return std::unexpected(copied.error()); }
  if (auto current = detail::ensureCurrent(*plugin); !current) { return std::unexpected(current.error()); }
  if (release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) {
    return error(ErrorCode::Conflict, "The signed release expired during staging");
  }

  const auto path = detail::pendingPath(release);
  const auto chain = detail::descend(plugin->original.get(), path, true);
  if (!chain) { return std::unexpected(chain.error()); }
  const auto parent = chain->back().get();
  if (!detail::isCurrentPath(plugin->original.get(), path, *chain)) {
    return error(ErrorCode::Conflict, "Pending release directory changed before publication");
  }
  if (auto current = detail::ensureCurrent(*plugin); !current) { return std::unexpected(current.error()); }
  auto disposition = PluginReleaseStage::Published;
  if (detail::linkPluginTemporary(temporary.get(), parent, detail::pendingAsset) != 0) {
    if (errno != EEXIST) { return error(ErrorCode::Io, "Could not publish the verified asset to its immutable pending slot"); }
    const auto existing = verifyExisting(parent, release.asset());
    if (!existing) { return std::unexpected(existing.error()); }
    disposition = *existing;
  }
  if (::fsync(parent) != 0) { return error(ErrorCode::Io, "Could not durably publish the pending release"); }
  if (!detail::isCurrentPath(plugin->original.get(), path, *chain)) {
    return error(ErrorCode::Conflict, "Pending release directory changed before proof publication");
  }
  if (auto current = detail::ensureCurrent(*plugin); !current) { return std::unexpected(current.error()); }
  if (auto saved = publishProof(parent, release); !saved) { return std::unexpected(saved.error()); }
  return disposition;
}

std::expected<VerifiedPluginRelease, Error> readStagedPluginRelease(const storage::ProfileLease& lease,
    std::string originalDirectory, const PluginReleaseTarget& target, QStringView version, qint64 sequence,
    QByteArrayView approvedPublicKey, qint64 lastSequence) {
  if (!pluginReleaseCatalogueUrl(target, version) || sequence <= 0 || lastSequence < 0 ||
      approvedPublicKey.size() != 32) {
    return error(ErrorCode::InvalidFormat, "A current staged version, sequence and approved signing key are required");
  }
  if (sequence <= lastSequence) { return error(ErrorCode::Conflict, "The staged release has already been superseded"); }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  const auto plugin = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), target);
  if (!plugin) { return std::unexpected(plugin.error()); }
  const auto path = detail::pendingPath(target, version);
  const auto chain = detail::descend(plugin->original.get(), path, false);
  if (!chain) { return std::unexpected(chain.error()); }
  const auto parent = chain->back().get();
  const auto leaf = proofName(sequence);
  const auto proof = detail::openArtifact(parent, leaf.c_str());
  QFile evidence;
  if (!proof || !evidence.open(proof->get(), QIODevice::ReadOnly) || evidence.size() <= proofTag.size() + signatureBytes ||
      evidence.size() > maximumProof) {
    return error(ErrorCode::Missing, "The selected signed staged proof is unavailable");
  }
  const auto encoded = evidence.read(maximumProof + 1);
  if (!evidence.atEnd() || !encoded.startsWith(proofTag) ||
      encoded.size() <= proofTag.size() + signatureBytes || encoded.size() > maximumProof) {
    return error(ErrorCode::InvalidFormat, "The staged release proof has an invalid envelope");
  }
  const QByteArrayView signedBytes(encoded);
  const auto signature = signedBytes.sliced(proofTag.size(), signatureBytes);
  const auto catalogue = signedBytes.sliced(proofTag.size() + signatureBytes);
  auto release = verifyPluginReleaseCatalogue(catalogue, signature, approvedPublicKey, target, lastSequence,
      QDateTime::currentSecsSinceEpoch());
  if (!release || release->sequence() != sequence || release->asset().version != version) {
    return error(ErrorCode::Conflict, "Cannot authenticate the selected staged release");
  }
  const auto artifact = detail::openArtifact(parent, detail::pendingAsset);
  QFile input;
  if (!artifact || !input.open(artifact->get(), QIODevice::ReadOnly) || !verifyPluginReleaseAsset(input, release->asset()) ||
      !detail::heldAt(parent, detail::pendingAsset, artifact->get()) || !detail::heldAt(parent, leaf, proof->get()) ||
      !detail::isCurrentPath(plugin->original.get(), path, *chain)) {
    return error(ErrorCode::Conflict, "The staged proof or asset changed since staging");
  }
  if (auto current = detail::ensureCurrent(*plugin); !current) { return std::unexpected(current.error()); }
  return std::move(*release);
}

std::expected<std::optional<VerifiedPluginRelease>, Error> findStagedPluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target,
    QByteArrayView approvedPublicKey, qint64 lastSequence) {
  if (!pluginReleaseCatalogueUrl(target, u"catalogue.json") || approvedPublicKey.size() != 32 || lastSequence < 0) {
    return error(ErrorCode::InvalidFormat, "An approved signing key and previous native sequence are required");
  }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  const auto plugin = detail::openInstalledJavaPlugin(lease, originalDirectory, target);
  if (!plugin) { return std::unexpected(plugin.error()); }

  const auto path = detail::pendingPath(target, {});
  const auto base = std::span(path).first(path.size() - 1);
  const auto chain = detail::descend(plugin->original.get(), base, false);
  if (!chain && chain.error().code == ErrorCode::Missing) { return std::nullopt; }
  if (!chain) { return std::unexpected(chain.error()); }
  const auto parent = chain->back().get();
  const auto directories = detail::childNames(parent, 128);
  if (!directories) { return std::unexpected(directories.error()); }

  qint64 highest = lastSequence;
  QString selectedVersion;
  bool ambiguous = false;
  for (const auto& name : *directories) {
    const auto version = QString::fromUtf8(name);
    if (!pluginReleaseCatalogueUrl(target, version)) { continue; }
    const auto slot = detail::directory(parent, name, false);
    if (!slot) { return std::unexpected(slot.error()); }
    const auto files = detail::childNames(slot->get(), 64);
    if (!files) { return std::unexpected(files.error()); }
    for (const auto& file : *files) {
      const auto sequence = proofSequence(file);
      if (!sequence || *sequence <= lastSequence || *sequence < highest) { continue; }
      if (*sequence == highest) {
        ambiguous = true;
        continue;
      }
      highest = *sequence;
      selectedVersion = version;
      ambiguous = false;
    }
  }
  if (!detail::isCurrentPath(plugin->original.get(), base, *chain)) {
    return error(ErrorCode::Conflict, "Pending plugin inventory changed during discovery");
  }
  if (auto current = detail::ensureCurrent(*plugin); !current) { return std::unexpected(current.error()); }
  if (ambiguous) { return error(ErrorCode::Conflict, "More than one pending version claims the newest sequence"); }
  if (selectedVersion.isEmpty()) { return std::nullopt; }
  auto release = readStagedPluginRelease(lease, std::move(originalDirectory), target, selectedVersion,
      highest, approvedPublicKey, lastSequence);
  if (!release) { return std::unexpected(release.error()); }
  return std::move(*release);
}

} // namespace biglybtc::update
