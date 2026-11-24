// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_plugin_updates.hpp"

#include "country_provider_binding.hpp"
#include "plugin_activation_store.hpp"
#include "plugin_update_workflow.hpp"
#include "biglybtc/plugin.h"

#include <QCoreApplication>
#include <QtGlobal>
#include <chrono>

namespace biglybtc::app {
namespace {
using namespace std::chrono_literals;
using namespace update;

QString label(const char* message) {
  return QCoreApplication::translate("NativePluginUpdates", message);
}

QString describe(PluginUpdateError error) {
  if (const auto* storage = std::get_if<storage::Error>(&error)) {
    return QString::fromStdString(storage->message);
  }
  if (const auto* policy = std::get_if<PluginUpdatePolicyError>(&error)) {
    return label("Invalid update setting: %1").arg(QString::fromStdString(policy->field));
  }
  if (const auto* transport = std::get_if<PluginDownloadError>(&error)) {
    switch (*transport) {
      case PluginDownloadError::Cancelled: return label("Plugin update cancelled.");
      case PluginDownloadError::Policy: return label("The saved network policy does not permit this update route.");
      case PluginDownloadError::Tls: return label("The update server’s TLS identity could not be verified.");
      case PluginDownloadError::Deadline: return label("The update server did not finish in time.");
      case PluginDownloadError::AssetIdentity: return label("The download does not match the signed release.");
      default: return label("Could not retrieve a valid release from the approved update channel.");
    }
  }
  return label("The update catalogue failed authentication, compatibility or replay checks.");
}

NativePluginVersion version(const VerifiedPluginRelease& release) {
  return {release.asset().version, release.sequence()};
}

std::optional<NativePluginVersion> version(const std::optional<PluginActivationEntry>& entry) {
  if (!entry) { return std::nullopt; }
  return version(entry->release);
}

void readPending(NativePluginStatus& status, const storage::ProfileLease& lease, const std::string& directory,
    const PluginReleaseTarget& target, QByteArrayView publicKey, qint64 floor) {
  const auto staged = findStagedPluginRelease(lease, directory, target, publicKey, floor);
  if (staged && *staged) { status.staged = version(**staged); }
  if (!staged && status.message.isEmpty()) { status.message = QString::fromStdString(staged.error().message); }
}

} // namespace

update::PluginReleaseTarget NativePluginUpdateChannel::target() {
  return {QStringLiteral("azlocprov"), QStringLiteral("d00rsfan/BiglyBTC-plugin-azlocprov"),
      QStringLiteral("linux-amd64"), QString::fromLatin1(QT_VERSION_STR), 1, BT_PLUGIN_ABI_V1};
}

NativePluginStatus NativePluginUpdateChannel::run(const storage::ProfileLease& lease,
    const NativePluginRequest& request, bool allowDownload, std::stop_token stop) const {
  NativePluginStatus status;
  status.session = request.session;
  if (stop.stop_requested()) {
    status.message = label("Plugin update cancelled.");
    return status;
  }
  const auto releaseTarget = target();
  const auto directory = findInstalledJavaPluginDirectory(lease, releaseTarget);
  if (!directory || !*directory) {
    status.message = directory ? label("The original Java location provider is not installed.")
        : QString::fromStdString(directory.error().message);
    return status;
  }
  auto installation = readPluginInstallation(lease, **directory, releaseTarget, publicKey);
  if (!installation) {
    status.message = QString::fromStdString(installation.error().message);
    return status;
  }
  status.canDownload = allowDownload;
  switch (request.action) {
    case NativePluginAction::Status:
      break;
    case NativePluginAction::Download: {
      if (!allowDownload) {
        status.message = label("Downloading updates requires a network-enabled profile.");
        break;
      }
      auto options = download;
      options.cancellation = stop;
      const auto result = checkInstalledPluginUpdate(lease, {}, releaseTarget, catalogueName, publicKey,
          UpdateCheckTrigger::Explicit, PluginUpdateIntent::ExplicitDownload, options);
      if (!result) { status.message = describe(result.error()); }
      else if (result->alreadyAccepted) { status.message = label("This signed release has already been accepted."); }
      else if (result->staged && result->release) {
        status.message = label("Verified version %1 is ready to install.").arg(result->release->asset().version);
      }
      break;
    }
    case NativePluginAction::Install:
    case NativePluginAction::Rollback: {
      const bool rollback = request.action == NativePluginAction::Rollback;
      if (rollback) {
        if (!request.selected || request.selected != version(installation->previous)) {
          status.message = label("The previous version changed. Review the current versions and retry.");
          break;
        }
      } else {
        readPending(status, lease, **directory, releaseTarget, publicKey, installation->highestSequence);
        if (!request.selected || request.selected != status.staged) {
          if (status.message.isEmpty()) {
            status.message = label("The staged version changed. Review the current versions and retry.");
          }
          break;
        }
        status.staged.reset();
      }
      auto changed = rollback
          ? rollbackPluginRelease(lease, **directory, releaseTarget, publicKey, trial, BT_CAP_LOCATION, 10s, stop)
          : activateStagedPluginRelease(lease, **directory, releaseTarget, request.selected->version,
              request.selected->sequence, publicKey, trial, BT_CAP_LOCATION, 10s, stop);
      if (!changed) {
        status.message = QString::fromStdString(changed.error().message);
      } else {
        installation = std::move(changed);
        status.message = label("Restart BiglyBTC to load version %1.").arg(installation->active->release.asset().version);
      }
      break;
    }
  }
  status.active = version(installation->active);
  status.previous = version(installation->previous);
  if (status.active && countryDatabase.empty()) {
    status.startupFailure = label("Select a country data directory with --country-db to load the signed location provider.");
  }
  if (!status.staged && !stop.stop_requested()) {
    readPending(status, lease, **directory, releaseTarget, publicKey, installation->highestSequence);
  }
  return status;
}

std::expected<NativePluginUpdateChannel::CountryStartup, storage::Error>
NativePluginUpdateChannel::startCountryProvider(const storage::ProfileLease& lease) const {
  if (countryDatabase.empty()) { return CountryStartup{}; }
  const auto releaseTarget = target();
  const auto directory = findInstalledJavaPluginDirectory(lease, releaseTarget);
  if (!directory) { return std::unexpected(directory.error()); }
  if (!*directory) { return CountryStartup{}; }
  const auto active = openActivatedPluginRelease(lease, **directory, releaseTarget, publicKey);
  if (!active) { return std::unexpected(active.error()); }
  if (!*active) { return CountryStartup{}; }
  const auto version = (**active).identity.release.asset().version;
  auto provider = storage::loadVerifiedCountryProvider((**active).descriptor.get(), version.toStdString(),
      countryDatabase);
  if (!provider) { return std::unexpected(provider.error()); }
  return CountryStartup{std::move(*provider), version};
}

} // namespace biglybtc::app
