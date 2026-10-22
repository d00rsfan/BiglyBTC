#include "biglybtc/effective_rate_limits.hpp"
#include "../engine/effective_rate_limits.hpp"

#include <libtorrent/settings_pack.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
std::size_t assertions = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
}
int main() {
  try {
    CHECK(core::effectiveSessionRateLimit({}) == core::EffectiveRateLimit{});
    CHECK(core::effectiveSessionRateLimit(-1) == core::EffectiveRateLimit{});
    CHECK(core::effectiveSessionRateLimit(std::numeric_limits<std::int32_t>::min()) == core::EffectiveRateLimit{});
    CHECK(core::effectiveSessionRateLimit(0) == (core::EffectiveRateLimit{core::RateLimitState::Unlimited,0}));
    CHECK(core::effectiveSessionRateLimit(1) == (core::EffectiveRateLimit{core::RateLimitState::Limited,1}));
    CHECK(core::effectiveSessionRateLimit(4096) == (core::EffectiveRateLimit{core::RateLimitState::Limited,4096}));
    CHECK(core::effectiveSessionRateLimit(std::numeric_limits<std::int32_t>::max())
        == (core::EffectiveRateLimit{core::RateLimitState::Limited,2147483646}));
    CHECK(core::validSessionRateLimits({}));
    CHECK(core::validEffectiveRateLimit({core::RateLimitState::Unlimited,0}));
    CHECK(!core::validEffectiveRateLimit({core::RateLimitState::Unlimited,1}));
    CHECK(!core::validEffectiveRateLimit({core::RateLimitState::Unavailable,1}));
    CHECK(!core::validEffectiveRateLimit({core::RateLimitState::Limited,0}));
    CHECK(!core::validEffectiveRateLimit({core::RateLimitState::Limited,-1}));
    CHECK(!core::validEffectiveRateLimit({core::RateLimitState::Limited,2147483647}));
    CHECK(!core::validEffectiveRateLimit({static_cast<core::RateLimitState>(9),0}));
    libtorrent::settings_pack settings;
    CHECK(engine::detail::projectEffectiveRateLimits(settings) == core::SessionRateLimits{});
    settings.set_int(libtorrent::settings_pack::download_rate_limit,0);
    CHECK(engine::detail::projectEffectiveRateLimits(settings)
        == (core::SessionRateLimits{{core::RateLimitState::Unlimited,0},{}}));
    settings.set_int(libtorrent::settings_pack::upload_rate_limit,32768);
    const auto known = engine::detail::projectEffectiveRateLimits(settings);
    CHECK(known == (core::SessionRateLimits{{core::RateLimitState::Unlimited,0},{core::RateLimitState::Limited,32768}}));
    CHECK(settings.get_int(libtorrent::settings_pack::download_rate_limit) == 0);
    CHECK(settings.get_int(libtorrent::settings_pack::upload_rate_limit) == 32768);
    settings.set_int(libtorrent::settings_pack::download_rate_limit,std::numeric_limits<int>::max());
    settings.set_int(libtorrent::settings_pack::upload_rate_limit,-1);
    CHECK(engine::detail::projectEffectiveRateLimits(settings)
        == (core::SessionRateLimits{{core::RateLimitState::Limited,2147483646},{}}));
    CHECK(settings.get_int(libtorrent::settings_pack::download_rate_limit) == std::numeric_limits<int>::max());
    CHECK(settings.get_int(libtorrent::settings_pack::upload_rate_limit) == -1);
    CHECK(known.upload.bytesPerSecond == 32768); // Owned value, not a settings alias.
    using Slots = core::UploadSlotState;
    using Proxy = core::SessionProxyMode;
    CHECK(!core::effectiveSessionConnectionLimit({}));
    for (const auto invalid : {0, -1, std::numeric_limits<std::int32_t>::min()}) {
      CHECK(!core::effectiveSessionConnectionLimit(invalid));
    }
    for (const auto limit : {1, 200, std::numeric_limits<std::int32_t>::max()}) {
      CHECK(core::effectiveSessionConnectionLimit(limit) == limit);
    }
    CHECK(core::effectiveSessionUploadSlots({}, {}) == core::EffectiveUploadSlots{});
    CHECK(core::effectiveSessionUploadSlots(8, {}) == core::EffectiveUploadSlots{});
    CHECK(core::effectiveSessionUploadSlots({}, false) == core::EffectiveUploadSlots{});
    CHECK(core::effectiveSessionUploadSlots({}, true) == (core::EffectiveUploadSlots{Slots::Automatic,0}));
    CHECK(core::effectiveSessionUploadSlots(0, false) == (core::EffectiveUploadSlots{Slots::Disabled,0}));
    CHECK(core::effectiveSessionUploadSlots(8, false) == (core::EffectiveUploadSlots{Slots::Limited,8}));
    for (const auto unlimited : {-1, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()}) {
      CHECK(core::effectiveSessionUploadSlots(unlimited, false) == (core::EffectiveUploadSlots{Slots::Unlimited,0}));
      CHECK(core::effectiveSessionUploadSlots(unlimited, true) == (core::EffectiveUploadSlots{Slots::Automatic,0}));
    }
    CHECK(core::validSessionConnectionSettings({}));
    CHECK(core::validSessionConnectionSettings({200,{Slots::Limited,8},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({0,{},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({-1,{},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {Slots::Automatic,8},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {Slots::Disabled,1},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {Slots::Limited,0},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {Slots::Limited,std::numeric_limits<std::int32_t>::max()},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {static_cast<Slots>(99),0},Proxy::Disabled}));
    CHECK(!core::validSessionConnectionSettings({{}, {},static_cast<Proxy>(99)}));
    core::SessionRateLimits invalidDiagnostics;
    invalidDiagnostics.connections.connectionLimit = 0;
    CHECK(!core::validSessionRateLimits(invalidDiagnostics));

    using Settings = libtorrent::settings_pack;
    libtorrent::settings_pack diagnostics;
    CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections == core::SessionConnectionSettings{});
    diagnostics.set_int(Settings::connections_limit,200);
    diagnostics.set_int(Settings::unchoke_slots_limit,8);
    diagnostics.set_int(Settings::proxy_type,Settings::none);
    const auto partial = engine::detail::projectEffectiveRateLimits(diagnostics).connections;
    CHECK(partial == (core::SessionConnectionSettings{200,{},Proxy::Disabled})); // Algorithm absent, not presumed fixed.
    diagnostics.set_int(Settings::choking_algorithm,Settings::fixed_slots_choker);
    const auto fixed = engine::detail::projectEffectiveRateLimits(diagnostics).connections;
    CHECK(fixed == (core::SessionConnectionSettings{200,{Slots::Limited,8},Proxy::Disabled}));
    diagnostics.set_int(Settings::connections_limit,0);
    diagnostics.set_int(Settings::unchoke_slots_limit,0);
    CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections
        == (core::SessionConnectionSettings{{},{Slots::Disabled,0},Proxy::Disabled}));
    diagnostics.set_int(Settings::unchoke_slots_limit,-1);
    CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections.uploadSlots
        == (core::EffectiveUploadSlots{Slots::Unlimited,0}));
    diagnostics.set_int(Settings::choking_algorithm,Settings::rate_based_choker);
    CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections.uploadSlots
        == (core::EffectiveUploadSlots{Slots::Automatic,0}));
    for (const auto unsupported : {-1, 1, 3, std::numeric_limits<std::int32_t>::max()}) {
      diagnostics.set_int(Settings::choking_algorithm,unsupported);
      CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections.uploadSlots == core::EffectiveUploadSlots{});
    }
    const std::array proxyCases{
        std::pair{Settings::none,Proxy::Disabled},std::pair{Settings::socks4,Proxy::Socks4},
        std::pair{Settings::socks5,Proxy::Socks5},std::pair{Settings::socks5_pw,Proxy::Socks5},
        std::pair{Settings::http,Proxy::Http},std::pair{Settings::http_pw,Proxy::Http}};
    diagnostics.set_str(Settings::proxy_hostname,"private-host-not-for-presentation.invalid");
    diagnostics.set_str(Settings::proxy_username,"private-user");
    diagnostics.set_str(Settings::proxy_password,"private-password");
    for (const auto& [input, expected] : proxyCases) {
      diagnostics.set_int(Settings::proxy_type,input);
      CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections.proxy == expected);
    }
    for (const auto unknown : {-1, 6, std::numeric_limits<std::int32_t>::max()}) {
      diagnostics.set_int(Settings::proxy_type,unknown);
      CHECK(engine::detail::projectEffectiveRateLimits(diagnostics).connections.proxy == Proxy::Unavailable);
    }
    CHECK(diagnostics.get_str(Settings::proxy_hostname) == "private-host-not-for-presentation.invalid");
    CHECK(diagnostics.get_str(Settings::proxy_username) == "private-user");
    CHECK(diagnostics.get_str(Settings::proxy_password) == "private-password");
    CHECK(fixed.connectionLimit == 200 && fixed.uploadSlots.count == 8 && fixed.proxy == Proxy::Disabled);
    std::cout << "PASS effective session-rate-limit projection; " << assertions
              << " assertions; explicit settings packs only, no session/network mutation\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL " << failure.what() << '\n'; return EXIT_FAILURE;
  }
}
