// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_activation_store.hpp"

#include "plugin_release_disk.hpp"
#include "plugin_release_slot.hpp"
#include "plugin_release_stage.hpp"
#include "plugin_release_slot_internal.hpp"

#include <QDateTime>
#include <QFile>
#include <QScopeGuard>
#include <QUuid>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::update {
namespace {
using storage::Error;
using storage::ErrorCode;
using storage::detail::Descriptor;
constexpr char pointerName[] = "installed.json";
constexpr std::string_view draftPrefix = ".activate-";
constexpr qint64 maximumPointer = 192 * 1024;

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

struct Pointer {
  PluginActivationState state;
  std::optional<Descriptor> descriptor;
  QByteArray raw;
};

std::expected<Pointer, Error> readPointer(
    int native, const PluginReleaseTarget& target, QByteArrayView publicKey, const char* name = pointerName) {
  struct stat info{};
  if (::fstatat(native, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) { return Pointer{}; }
    return error(ErrorCode::Io, "Cannot inspect the native activation pointer");
  }
  auto pointer = detail::openArtifact(native, name);
  QFile source;
  if (!pointer || !source.open(pointer->get(), QIODevice::ReadOnly) || source.size() <= 0 ||
      source.size() > maximumPointer) {
    return error(ErrorCode::Conflict, "The installed native plugin pointer is not an owned activation record");
  }
  auto raw = source.read(maximumPointer + 1);
  auto state = readPluginActivationState(raw, publicKey, target, QDateTime::currentSecsSinceEpoch());
  if (!source.atEnd() || !state || !detail::heldAt(native, name, pointer->get())) {
    return error(ErrorCode::Conflict, "Cannot authenticate the installed native plugin pointer");
  }
  return Pointer{std::move(*state), std::move(*pointer), std::move(raw)};
}

bool pointerUnchanged(int native, const Pointer& expected, const char* name = pointerName) {
  if (!expected.descriptor) {
    struct stat info{};
    return ::fstatat(native, name, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
  }
  QFile source;
  return detail::heldAt(native, name, expected.descriptor->get()) &&
      source.open(expected.descriptor->get(), QIODevice::ReadOnly) && source.seek(0) &&
      source.size() == expected.raw.size() && source.read(expected.raw.size() + 1) == expected.raw && source.atEnd();
}

void discardSupersededReceipts(int native, const Pointer& before, const PluginActivationEntry& requested,
    QByteArrayView encoded) {
  const auto names = detail::childNames(native, 256);
  if (!names) { return; }
  for (const auto& name : *names) {
    if (!name.starts_with(draftPrefix) || name.size() != draftPrefix.size() + 36) { continue; }
    const auto suffix = QString::fromLatin1(name.data() + draftPrefix.size(), 36);
    const auto uuid = QUuid::fromString(suffix);
    if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != suffix) { continue; }
    auto draft = readPointer(native, requested.release.target(), requested.release.signingKey(), name.c_str());
    if (!draft || !draft->descriptor || !draft->state.active ||
        encodePluginActivationState(draft->state) != draft->raw) {
      continue;
    }
    // The pending switch can be retried later, after repeating the trial.
    // Only the timestamp of the candidate acceptance may then differ.
    draft->state.active->acceptedAt = requested.acceptedAt;
    if (encodePluginActivationState(draft->state) == encoded &&
        pointerUnchanged(native, before) && pointerUnchanged(native, *draft, name.c_str())) {
      ::unlinkat(native, name.c_str(), 0);
    }
  }
}

std::expected<PluginActivationState, Error> publishPointer(const detail::InstalledJavaPlugin& original, int native,
    const Pointer& before, PluginActivationState next, std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native plugin activation was cancelled"); }
  const auto encoded = encodePluginActivationState(next);
  if (encoded.isEmpty() || encoded.size() > maximumPointer) {
    return error(ErrorCode::InvalidFormat, "The verified activation pointer exceeds its size bound");
  }
  const auto staged = detail::preparePluginReceipt(native, encoded);
  if (!staged) { return std::unexpected(staged.error()); }
  if (const auto current = detail::ensureCurrent(original); !current) { return std::unexpected(current.error()); }
  if (!detail::heldAt(original.original.get(), "biglybtc", native) || !pointerUnchanged(native, before)) {
    return error(ErrorCode::Conflict, "Native activation state changed before the switch");
  }

  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native plugin activation was cancelled"); }
  if (!before.descriptor) {
    if (detail::linkPluginTemporary(staged->get(), native, pointerName) != 0 || ::fsync(native) != 0) {
      return error(ErrorCode::Io, "Cannot durably publish the first native activation pointer");
    }
    return next;
  }

  if (next.active) { discardSupersededReceipts(native, before, *next.active, encoded); }
  const auto leaf = std::string(draftPrefix) + QUuid::createUuid().toByteArray(QUuid::WithoutBraces).toStdString();
  if (detail::linkPluginTemporary(staged->get(), native, leaf.c_str()) != 0) {
    return error(ErrorCode::Io, "Cannot name the synchronized native activation receipt");
  }
  auto cleanup = qScopeGuard([&] {
    if (detail::heldAt(native, leaf, staged->get()) && ::unlinkat(native, leaf.c_str(), 0) == 0) {
      ::fsync(native);
    }
  });
  if (!detail::heldAt(native, leaf, staged->get()) || !pointerUnchanged(native, before) ||
      !detail::heldAt(original.original.get(), "biglybtc", native)) {
    return error(ErrorCode::Conflict, "Native activation state changed before the switch");
  }
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native plugin activation was cancelled"); }
  if (::renameat2(native, leaf.c_str(), native, pointerName, 0) != 0 || ::fsync(native) != 0) {
    return error(ErrorCode::Io, "Cannot durably publish the native activation pointer");
  }
  cleanup.dismiss();
  return next;
}
} // namespace

std::expected<PluginActivationState, Error> readPluginInstallation(const storage::ProfileLease& lease,
    std::string originalDirectory, const PluginReleaseTarget& target, QByteArrayView publicKey) {
  if (publicKey.size() != 32) { return error(ErrorCode::InvalidFormat, "An approved plugin signing key is required"); }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  const auto original = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), target);
  if (!original) { return std::unexpected(original.error()); }
  const auto native = detail::directory(original->original.get(), "biglybtc", false);
  if (!native) {
    if (native.error().code == ErrorCode::Missing) { return PluginActivationState{}; }
    return std::unexpected(native.error());
  }
  auto pointer = readPointer(native->get(), target, publicKey);
  if (!pointer) { return std::unexpected(pointer.error()); }
  if (!detail::heldAt(original->original.get(), "biglybtc", native->get()) ||
      !pointerUnchanged(native->get(), *pointer)) {
    return error(ErrorCode::Conflict, "The native plugin installation changed during update admission");
  }
  return std::move(pointer->state);
}

std::expected<qint64, Error> readPluginReleaseReplayFloor(const storage::ProfileLease& lease,
    std::string originalDirectory, const PluginReleaseTarget& target, QByteArrayView publicKey) {
  const auto installation = readPluginInstallation(lease, std::move(originalDirectory), target, publicKey);
  if (!installation) { return std::unexpected(installation.error()); }
  return installation->highestSequence;
}

std::expected<std::optional<ActivatedPluginModule>, Error> openActivatedPluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target,
    QByteArrayView publicKey) {
  if (publicKey.size() != 32) { return error(ErrorCode::InvalidFormat, "An approved plugin signing key is required"); }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  const auto original = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), target);
  if (!original) { return std::unexpected(original.error()); }
  const auto native = detail::directory(original->original.get(), "biglybtc", false);
  if (!native) {
    if (native.error().code == ErrorCode::Missing) { return std::nullopt; }
    return std::unexpected(native.error());
  }
  auto pointer = readPointer(native->get(), target, publicKey);
  if (!pointer) { return std::unexpected(pointer.error()); }
  if (!pointer->state.active) { return std::nullopt; }
  auto verified = detail::checkedInstalledBundle(*original, pointer->state.active->release);
  if (!verified) { return std::unexpected(verified.error()); }
  if (!detail::heldAt(original->original.get(), "biglybtc", native->get()) ||
      !pointerUnchanged(native->get(), *pointer)) {
    return error(ErrorCode::Conflict, "The active plugin changed during startup admission");
  }
  const auto originalDirectoryFd = ::fcntl(original->original.get(), F_DUPFD_CLOEXEC, 3);
  if (originalDirectoryFd < 0) { return error(ErrorCode::Io, "Cannot retain the current original plugin folder"); }
  return ActivatedPluginModule{std::move(*pointer->state.active), std::move(verified->descriptor),
      Descriptor(originalDirectoryFd), original->lease};
}

std::expected<PluginActivationState, Error> activatePluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory, const VerifiedPluginRelease& release,
    const PluginTrialProgram& program, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native installation work was cancelled"); }
  const auto original = detail::openInstalledJavaPlugin(lease, originalDirectory, release.target());
  if (!original) { return std::unexpected(original.error()); }
  const auto native = detail::directory(original->original.get(), "biglybtc", false);
  if (!native) { return std::unexpected(native.error()); }
  const auto before = readPointer(native->get(), release.target(), release.signingKey());
  if (!before) { return std::unexpected(before.error()); }
  if (!selectPluginActivation(before->state, release, QDateTime::currentSecsSinceEpoch())) {
    return error(ErrorCode::Conflict, "Release conflicts with the recorded native installation");
  }
  const auto prepared = publishPluginReleaseSlot(lease, std::move(originalDirectory), release, program, capabilities, deadline, stop);
  if (!prepared) { return std::unexpected(prepared.error()); }
  const auto access = original->lease->sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  const auto bundle = detail::checkedInstalledBundle(*original, release);
  if (!bundle) { return std::unexpected(bundle.error()); }
  auto selected = selectPluginActivation(before->state, release, QDateTime::currentSecsSinceEpoch());
  if (!selected) { return error(ErrorCode::Conflict, "The signed activation is no longer current"); }
  return publishPointer(*original, native->get(), *before, std::move(*selected), stop);
}

std::expected<PluginActivationState, Error> activateStagedPluginRelease(const storage::ProfileLease& lease,
    std::string originalDirectory, const PluginReleaseTarget& target, QStringView version, qint64 sequence,
    QByteArrayView approvedPublicKey, const PluginTrialProgram& program, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native installation work was cancelled"); }
  const auto floor = readPluginReleaseReplayFloor(lease, originalDirectory, target, approvedPublicKey);
  if (!floor) { return std::unexpected(floor.error()); }
  const auto release = readStagedPluginRelease(lease, originalDirectory, target, version, sequence, approvedPublicKey, *floor);
  if (!release) { return std::unexpected(release.error()); }
  return activatePluginRelease(lease, std::move(originalDirectory), *release, program, capabilities, deadline, stop);
}

std::expected<std::optional<PluginActivationState>, Error> activateLatestStagedPluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target,
    QByteArrayView approvedPublicKey, const PluginTrialProgram& program, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native installation work was cancelled"); }
  const auto floor = readPluginReleaseReplayFloor(lease, originalDirectory, target, approvedPublicKey);
  if (!floor) { return std::unexpected(floor.error()); }
  const auto release = findStagedPluginRelease(lease, originalDirectory, target, approvedPublicKey, *floor);
  if (!release) { return std::unexpected(release.error()); }
  if (!*release) { return std::nullopt; }
  auto activated = activatePluginRelease(lease, std::move(originalDirectory), **release, program, capabilities, deadline, stop);
  if (!activated) { return std::unexpected(activated.error()); }
  return std::move(*activated);
}

std::expected<PluginActivationState, Error> rollbackPluginRelease(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target,
    QByteArrayView publicKey, const PluginTrialProgram& program, std::uint64_t capabilities,
    std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native installation work was cancelled"); }
  const auto original = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), target);
  if (!original) { return std::unexpected(original.error()); }
  const auto native = detail::directory(original->original.get(), "biglybtc", false);
  if (!native) { return std::unexpected(native.error()); }
  const auto before = readPointer(native->get(), target, publicKey);
  if (!before) { return std::unexpected(before.error()); }
  auto next = selectPluginRollback(before->state);
  if (!next) { return error(ErrorCode::Missing, "No authenticated previous native plugin is available for rollback"); }
  const auto& selected = next->active->release;
  const auto candidate = detail::checkedInstalledBundle(*original, selected);
  if (!candidate) { return std::unexpected(candidate.error()); }
  const auto trial = detail::trialPluginModule(candidate->bundle, program, capabilities, deadline, stop);
  if (!trial) {
    return trial.error() == PluginTrialError::Cancelled
        ? error(ErrorCode::Cancelled, "The rollback trial was cancelled")
        : error(ErrorCode::Unsupported, "The previous native plugin failed its isolated trial");
  }
  const auto access = original->lease->sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  if (const auto exact = detail::checkedInstalledBundle(*original, selected); !exact) {
    return std::unexpected(exact.error());
  }
  return publishPointer(*original, native->get(), *before, std::move(*next), stop);
}

} // namespace biglybtc::update
