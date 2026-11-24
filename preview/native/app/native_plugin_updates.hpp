// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "native_plugin_update_status.hpp"
#include "country_provider.hpp"
#include "plugin_release_trial.hpp"
#include "plugin_release_transport.hpp"
#include "profile.hpp"

#include <filesystem>

namespace biglybtc::ui { class LibraryWindow; }

namespace biglybtc::app {

class SessionBridge;

// Package-owned settings. The profile supplies original update policy and
// plugin installation identity. It cannot select a channel or signing key.
struct NativePluginUpdateChannel {
  QByteArray publicKey;
  update::PluginTrialProgram trial;
  update::PluginDownloadOptions download;
  std::filesystem::path countryDatabase;

  static update::PluginReleaseTarget target();
  static constexpr auto catalogueName = u"catalogue.json";

  NativePluginStatus run(const storage::ProfileLease&, const NativePluginRequest&, bool allowDownload,
      std::stop_token) const;

  struct CountryStartup {
    std::unique_ptr<storage::CountryProvider> provider;
    QString version;
  };
  std::expected<CountryStartup, storage::Error> startCountryProvider(const storage::ProfileLease&) const;
};

void bindNativePluginUpdateControls(ui::LibraryWindow&, SessionBridge&);

} // namespace biglybtc::app
