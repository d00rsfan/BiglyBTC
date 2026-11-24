// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_slot.hpp"

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
constexpr char Module[] = "plugin.so";
constexpr char Manifest[] = "manifest.json";

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

std::string_view bytes(const QByteArray& data) {
  return {data.constData(), static_cast<std::size_t>(data.size())};
}

std::optional<Descriptor> matchingFile(int parent, const char* name, const QByteArray& expected) {
  auto file = detail::openArtifact(parent, name);
  QFile input;
  if (!file || !input.open(file->get(), QIODevice::ReadOnly) || input.size() != expected.size() ||
      input.read(expected.size() + 1) != expected || !input.atEnd() || !detail::heldAt(parent, name, file->get())) {
    return std::nullopt;
  }
  return std::move(*file);
}

struct PendingBundle {
  std::array<std::string, 5> path;
  std::vector<Descriptor> directories;
  Descriptor artifact;
  VerifiedPluginBundle verified;
};

std::expected<PendingBundle, Error> pendingBundle(
    const detail::InstalledJavaPlugin& original, const VerifiedPluginRelease& release) {
  auto path = detail::pendingPath(release);
  auto directories = detail::descend(original.original.get(), path, false);
  if (!directories) { return std::unexpected(directories.error()); }
  auto asset = detail::openArtifact(directories->back().get(), detail::pendingAsset);
  QFile input;
  if (!asset || !input.open(asset->get(), QIODevice::ReadOnly)) {
    return error(ErrorCode::Conflict, "No immutable pending release asset is available");
  }
  auto bundle = readPluginReleaseBundle(input, release);
  if (!bundle) { return error(ErrorCode::InvalidFormat, "The pending release package is invalid"); }
  return PendingBundle{std::move(path), std::move(*directories), std::move(*asset), std::move(*bundle)};
}

bool pendingCurrent(const detail::InstalledJavaPlugin& original, const PendingBundle& pending) {
  QFile input;
  return detail::isCurrentPath(original.original.get(), pending.path, pending.directories) &&
      detail::heldAt(pending.directories.back().get(), detail::pendingAsset, pending.artifact.get()) &&
      input.open(pending.artifact.get(), QIODevice::ReadOnly) && verifyPluginReleaseAsset(input, pending.verified.release().asset());
}

void removeTemporary(int parent, const std::string& stage, int directory,
    const std::expected<Descriptor, Error>& module, const std::expected<Descriptor, Error>& manifest) noexcept {
  if (!detail::heldAt(parent, stage, directory)) { return; }
  if (module && detail::heldAt(directory, Module, module->get())) { ::unlinkat(directory, Module, 0); }
  if (manifest && detail::heldAt(directory, Manifest, manifest->get())) { ::unlinkat(directory, Manifest, 0); }
  if (::unlinkat(parent, stage.c_str(), AT_REMOVEDIR) == 0) { ::fsync(parent); }
}
} // namespace

std::expected<detail::CheckedInstalledSlot, Error> detail::checkedInstalledBundle(
    const InstalledJavaPlugin& original, const VerifiedPluginRelease& release) {
  auto pending = pendingBundle(original, release);
  if (!pending) { return std::unexpected(pending.error()); }
  const auto destination = detail::versionParent(release);
  const auto chain = detail::descend(original.original.get(), destination, false);
  if (!chain) { return std::unexpected(chain.error()); }
  const auto version = release.asset().version.toStdString();
  const auto parent = chain->back().get();
  const auto slot = detail::directory(parent, version, false);
  if (!slot) { return std::unexpected(slot.error()); }
  auto module = matchingFile(slot->get(), Module, pending->verified.module());
  if (!module || !matchingFile(slot->get(), Manifest, pending->verified.manifest()) ||
      !detail::heldAt(parent, version, slot->get()) || !detail::isCurrentPath(original.original.get(), destination, *chain) ||
      !pendingCurrent(original, *pending)) {
    return error(ErrorCode::Conflict, "The installed version no longer matches its signed artifact");
  }
  if (const auto current = detail::ensureCurrent(original); !current) { return std::unexpected(current.error()); }
  return CheckedInstalledSlot{std::move(pending->verified), std::move(*module)};
}

std::expected<PluginSlotPublication, Error> publishPluginReleaseSlot(
    const storage::ProfileLease& lease, std::string originalDirectory, const VerifiedPluginRelease& release,
    const PluginTrialProgram& program, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native version preparation was cancelled"); }
  if (release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) {
    return error(ErrorCode::Unsupported, "The signed plugin release expired before preparation");
  }
  const auto original = detail::openInstalledJavaPlugin(lease, std::move(originalDirectory), release.target());
  if (!original) { return std::unexpected(original.error()); }
  const auto pending = pendingBundle(*original, release);
  if (!pending) { return std::unexpected(pending.error()); }
  const auto& bundle = pending->verified;
  const auto trial = trialPluginRelease(bundle, program, capabilities, deadline, stop);
  if (!trial) {
    return trial.error() == PluginTrialError::Cancelled
        ? error(ErrorCode::Cancelled, "The isolated native plugin trial was cancelled")
        : error(ErrorCode::Unsupported, "The isolated native plugin trial failed");
  }

  const auto access = original->lease->sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native version preparation was cancelled"); }
  if (const auto current = detail::ensureCurrent(*original); !current) { return std::unexpected(current.error()); }
  if (!pendingCurrent(*original, *pending) || release.expiresAt() <= QDateTime::currentSecsSinceEpoch()) {
    return error(ErrorCode::Conflict, "The pending release changed before publication");
  }
  const auto destination = detail::versionParent(release);
  const auto chain = detail::descend(original->original.get(), destination, true);
  if (!chain) { return std::unexpected(chain.error()); }
  const auto parent = chain->back().get();
  const auto version = release.asset().version.toStdString();
  const auto existing = detail::directory(parent, version, false);
  if (existing) {
    if (!matchingFile(existing->get(), Module, bundle.module()) ||
        !matchingFile(existing->get(), Manifest, bundle.manifest()) ||
        !detail::heldAt(parent, version, existing->get()) ||
        !detail::isCurrentPath(original->original.get(), destination, *chain)) {
      return error(ErrorCode::Conflict, "Existing version has a different native artifact");
    }
    if (::fsync(parent) != 0) { return error(ErrorCode::Io, "Cannot synchronize the existing version slot"); }
    return PluginSlotPublication::AlreadyPresent;
  }
  if (existing.error().code != ErrorCode::Missing) { return std::unexpected(existing.error()); }

  const auto temporary = ".install-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces).toStdString();
  if (::mkdirat(parent, temporary.c_str(), 0700) != 0) {
    return error(ErrorCode::Io, "Cannot create an exclusive native version directory");
  }
  const auto work = detail::directory(parent, temporary, false);
  if (!work) { return std::unexpected(work.error()); }
  const auto module = storage::detail::writeExclusiveOwned(work->get(), Module, bytes(bundle.module()), 0400);
  const auto manifest = module ? storage::detail::writeExclusiveOwned(work->get(), Manifest, bytes(bundle.manifest()), 0400) :
      std::expected<Descriptor, Error>(error(ErrorCode::Io, "Cannot stage native manifest without a module"));
  auto cleanup = qScopeGuard([&] { removeTemporary(parent, temporary, work->get(), module, manifest); });
  if (!module || !manifest || ::fsync(work->get()) != 0) {
    return error(ErrorCode::Io, "Cannot synchronize the complete native version");
  }
  if (!detail::heldAt(work->get(), Module, module->get()) || !detail::heldAt(work->get(), Manifest, manifest->get()) ||
      !detail::heldAt(parent, temporary, work->get()) ||
      !detail::isCurrentPath(original->original.get(), destination, *chain)) {
    return error(ErrorCode::Conflict, "The prepared native slot changed before publication");
  }
  if (const auto current = detail::ensureCurrent(*original); !current) { return std::unexpected(current.error()); }
  if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Native version preparation was cancelled"); }
  if (::renameat2(parent, temporary.c_str(), parent, version.c_str(), RENAME_NOREPLACE) != 0) {
    return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Cannot exclusively publish the verified native version");
  }
  cleanup.dismiss();
  if (::fsync(parent) != 0) { return error(ErrorCode::Io, "Cannot durably publish native slot"); }
  return PluginSlotPublication::Published;
}

} // namespace biglybtc::update
