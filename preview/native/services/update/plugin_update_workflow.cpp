// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_update_workflow.hpp"

#include "plugin_activation_store.hpp"
#include "plugin_release_disk.hpp"

#include <QBuffer>
#include <tuple>

namespace biglybtc::update {
namespace {
using storage::Error;
using storage::ErrorCode;

std::expected<PluginUpdatePlan, PluginUpdateError> currentPlan(const storage::ProfileLease& lease, UpdateCheckTrigger trigger) {
  auto file = storage::ConfigFile::load(lease, "biglybt.config");
  if (!file) { return std::unexpected(PluginUpdateError{file.error()}); }
  if (file->recoverySource() != storage::RecoverySource::Primary) {
    return std::unexpected(PluginUpdateError{Error{ErrorCode::Conflict, "The original update configuration is in recovery"}});
  }
  auto plan = planPluginUpdate(&file->document(), trigger);
  if (!plan) { return std::unexpected(PluginUpdateError{plan.error()}); }
  return std::move(*plan);
}

bool samePlan(const PluginUpdatePlan& prior, const PluginUpdatePlan& current) {
  if (prior.route != current.route || prior.downloadAutomatically != current.downloadAutomatically ||
      prior.proxy.has_value() != current.proxy.has_value()) {
    return false;
  }
  if (!prior.proxy) { return true; }
  const auto endpoint = [](const auto& proxy) {
    return std::tie(proxy.kind, proxy.host, proxy.port, proxy.username, proxy.password, proxy.remoteDns);
  };
  return endpoint(*prior.proxy) == endpoint(*current.proxy);
}

std::unexpected<PluginUpdateError> cancelled() {
  return std::unexpected(PluginUpdateError{PluginDownloadError::Cancelled});
}

std::unexpected<PluginUpdateError> changed() {
  return std::unexpected(PluginUpdateError{Error{ErrorCode::Conflict, "Update settings or native installation changed"}});
}

bool sameActivationEntry(const std::optional<PluginActivationEntry>& prior,
    const std::optional<PluginActivationEntry>& current) {
  if (!prior || !current) { return prior.has_value() == current.has_value(); }
  return prior->acceptedAt == current->acceptedAt &&
      prior->release.catalogue() == current->release.catalogue() &&
      prior->release.signature() == current->release.signature();
}

std::expected<void, PluginUpdateError> recheck(const detail::InstalledJavaPlugin& original, const PluginReleaseTarget& target,
    QByteArrayView key, const PluginActivationState& installation, UpdateCheckTrigger trigger,
    const PluginUpdatePlan& plan, bool discover) {
  if (discover) {
    const auto found = findInstalledJavaPluginDirectory(*original.lease, target);
    if (!found) { return std::unexpected(PluginUpdateError{found.error()}); }
    if (*found != original.binding.directoryName) { return changed(); }
  }
  const auto admitted = detail::ensureCurrent(original);
  if (!admitted) { return std::unexpected(PluginUpdateError{admitted.error()}); }
  const auto selected = currentPlan(*original.lease, trigger);
  if (!selected) { return std::unexpected(selected.error()); }
  if (!samePlan(*selected, plan)) { return changed(); }
  const auto current = readPluginInstallation(*original.lease, original.binding.directoryName, target, key);
  if (!current) { return std::unexpected(PluginUpdateError{current.error()}); }
  if (current->highestSequence != installation.highestSequence ||
      !sameActivationEntry(installation.active, current->active) ||
      !sameActivationEntry(installation.previous, current->previous)) {
    return changed();
  }
  return {};
}
} // namespace

std::expected<PluginUpdateOutcome, PluginUpdateError> checkInstalledPluginUpdate(
    const storage::ProfileLease& lease, std::string originalDirectory, const PluginReleaseTarget& target,
    QStringView catalogueName, QByteArrayView approvedPublicKey, UpdateCheckTrigger trigger,
    PluginUpdateIntent intent, const PluginDownloadOptions& options) {
  if (options.cancellation.stop_requested()) { return cancelled(); }
  if (intent == PluginUpdateIntent::ExplicitDownload && trigger != UpdateCheckTrigger::Explicit) {
    return std::unexpected(PluginUpdateError{PluginDownloadError::Policy});
  }
  const auto plan = currentPlan(lease, trigger);
  if (!plan) { return std::unexpected(plan.error()); }
  if (plan->route == UpdateNetworkRoute::Skipped) { return PluginUpdateOutcome{}; }

  const bool discover = originalDirectory.empty();
  if (discover) {
    auto found = findInstalledJavaPluginDirectory(lease, target);
    if (!found) { return std::unexpected(PluginUpdateError{found.error()}); }
    if (!*found) {
      return std::unexpected(PluginUpdateError{Error{ErrorCode::Missing, "The matching original Java plugin is not installed"}});
    }
    originalDirectory = std::move(**found);
  }
  const auto original = detail::openInstalledJavaPlugin(lease, originalDirectory, target);
  if (!original) { return std::unexpected(PluginUpdateError{original.error()}); }
  const auto installation = readPluginInstallation(*original->lease, originalDirectory, target, approvedPublicKey);
  if (!installation) { return std::unexpected(PluginUpdateError{installation.error()}); }
  const auto floor = installation->highestSequence;
  auto release = checkPluginReleaseCatalogue(target, catalogueName, approvedPublicKey,
      floor > 0 ? floor - 1 : 0, *plan, options);
  if (!release) {
    return std::unexpected(std::visit([](auto value) -> PluginUpdateError { return value; }, release.error()));
  }
  const auto stillAdmitted = recheck(*original, target, approvedPublicKey, *installation, trigger, *plan, discover);
  if (!stillAdmitted) { return std::unexpected(stillAdmitted.error()); }
  if (options.cancellation.stop_requested()) { return cancelled(); }
  if (release->sequence() == floor) {
    const auto matches = [&](const std::optional<PluginActivationEntry>& entry) {
      return entry && entry->release.sequence() == floor && entry->release.catalogue() == release->catalogue() &&
          entry->release.signature() == release->signature();
    };
    if (matches(installation->active) || matches(installation->previous)) {
      return PluginUpdateOutcome{std::move(*release), {}, true};
    }
    return std::unexpected(PluginUpdateError{CatalogueError::Replay});
  }
  const bool shouldStage = intent == PluginUpdateIntent::ExplicitDownload ||
      (intent == PluginUpdateIntent::FollowSavedDownload && plan->downloadAutomatically);
  if (!shouldStage) { return PluginUpdateOutcome{std::move(*release), {}}; }

  auto bytes = downloadPluginReleaseAsset(*release, *plan, options);
  if (!bytes) { return std::unexpected(PluginUpdateError{bytes.error()}); }
  QBuffer artifact(&*bytes);
  if (!artifact.open(QIODevice::ReadOnly)) {
    return std::unexpected(PluginUpdateError{PluginDownloadError::AssetIdentity});
  }
  const auto access = original->lease->sourceAccess(true);
  if (!access) { return std::unexpected(PluginUpdateError{access.error()}); }
  const auto publishable = recheck(*original, target, approvedPublicKey, *installation, trigger, *plan, discover);
  if (!publishable) { return std::unexpected(publishable.error()); }
  if (options.cancellation.stop_requested()) { return cancelled(); }
  const auto staged = stagePluginRelease(*original->lease, std::move(originalDirectory), *release, artifact);
  if (!staged) { return std::unexpected(PluginUpdateError{staged.error()}); }
  return PluginUpdateOutcome{std::move(*release), *staged};
}

} // namespace biglybtc::update
