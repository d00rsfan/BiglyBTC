// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_update_policy.hpp"

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace biglybtc::update;

namespace {
void require(bool passed, const char* message) {
  if (!passed) { throw std::runtime_error(message); }
}

auto parsed(std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
  auto doc = biglybtc::core::Document::parse("de");
  require(bool(doc), "Create original settings dictionary");
  for (const auto& [key, encoded] : fields) {
    require(bool(doc->replaceValue(key, std::string(encoded))), "Configure an original update preference");
  }
  return std::move(*doc);
}
} // namespace

int main() try {
  using Route = UpdateNetworkRoute;
  using Trigger = UpdateCheckTrigger;
  const auto defaults = planPluginUpdate(nullptr, Trigger::Startup);
  require(defaults && defaults->route == Route::Direct && !defaults->downloadAutomatically && !defaults->proxy,
      "Apply original startup and explicit download defaults");
  auto noTimers = parsed({{"update.start", "i0e"}, {"update.periodic", "i0e"}});
  require(planPluginUpdate(&noTimers, Trigger::Startup)->route == Route::Skipped &&
      planPluginUpdate(&noTimers, Trigger::Periodic)->route == Route::Skipped &&
      planPluginUpdate(&noTimers, Trigger::Explicit)->route == Route::Direct, "Honor disabled background checks and explicit checks");
  auto periodic = parsed({{"update.start", "i0e"}, {"update.periodic", "i1e"}, {"update.autodownload", "i1e"}});
  const auto alsoAtStart = planPluginUpdate(&periodic, Trigger::Startup);
  require(alsoAtStart && alsoAtStart->route == Route::Direct && alsoAtStart->downloadAutomatically,
      "Original periodic preference also checks at startup");
  auto peerOnly = parsed({{"Proxy.Data.Enable", "i1e"}, {"Proxy.Data.Host", "9:peer.test"}});
  require(planPluginUpdate(&peerOnly, Trigger::Explicit)->route == Route::Direct,
      "Peer-only proxy configuration does not select a web proxy");
  auto http = parsed({{"Enable.Proxy", "i1e"}, {"Proxy.Host", "10:proxy.test"}, {"Proxy.Port", "4:8118"},
      {"Proxy.Username", "6:<NoNe>"}});
  const auto httpPlan = planPluginUpdate(&http, Trigger::Explicit);
  require(httpPlan && httpPlan->route == Route::WebProxy && httpPlan->proxy &&
      httpPlan->proxy->kind == biglybtc::engine::NetworkProxyKind::Http && httpPlan->proxy->host == "proxy.test" &&
      httpPlan->proxy->port == 8118 && httpPlan->proxy->username.empty(), "Pass the saved original web proxy without default authentication");
  require(bool(http.replaceValue("Enable.SOCKS", "i1e")), "Use the original SOCKS switch");
  const auto socks = planPluginUpdate(&http, Trigger::Explicit);
  require(socks && socks->route == Route::WebProxy && socks->proxy && socks->proxy->remoteDns &&
      socks->proxy->kind == biglybtc::engine::NetworkProxyKind::Socks5, "Keep name resolution on the original SOCKS route");
  require(bool(http.replaceValue("update.anonymous", "i1e")), "Enable anonymous update selection");
  const auto blocked = planPluginUpdate(&http, Trigger::Explicit);
  require(blocked && blocked->route == Route::AnonymousBlocked && !blocked->proxy,
      "Follow the original active SOCKS suppression of plugin proxies");
  require(bool(http.replaceValue("Proxy.SOCKS.disable.plugin.proxies", "i0e")), "Restore provider admission");
  const auto needsProvider = planPluginUpdate(&http, Trigger::Startup);
  require(needsProvider && needsProvider->route == Route::NeedsAnonymousProvider && !needsProvider->proxy,
      "Anonymous updates require an actual provider without direct or saved SOCKS fallback");
  const auto alone = parsed({{"update.anonymous", "i1e"}});
  require(planPluginUpdate(&alone, Trigger::Explicit)->route == Route::NeedsAnonymousProvider,
      "An anonymous update without a qualified provider still selects a provider-only route");
  const auto malformed = parsed({{"Enable.Proxy", "i1e"}, {"Proxy.Host", "10:proxy.test"},
      {"Proxy.Port", "6:secret"}});
  const auto refused = planPluginUpdate(&malformed, Trigger::Explicit);
  require(!refused && refused.error().field == "Proxy.Port", "An invalid proxy is refused without echoing its saved value");
  const auto malformedMode = parsed({{"update.anonymous", "3:bad"}});
  const auto refusedFlag = planPluginUpdate(&malformedMode, Trigger::Explicit);
  require(!refusedFlag && refusedFlag.error().field == "update.anonymous", "An invalid anonymous flag cannot admit a direct route");
  std::cout << "PASS original update scheduling and explicit direct, proxy and anonymous route selection\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
