#include "network_policy.hpp"
#include "persistent_runtime_guard.hpp"

#include <utility>

#if defined(__linux__) && __has_include(<libtorrent/biglybtc_socks4a.hpp>)
#define BIGLYBTC_SOCKS4_PROVIDER_SDK 1
#include <libtorrent/biglybtc_socks4a.hpp>
#include <dlfcn.h>
#else
#define BIGLYBTC_SOCKS4_PROVIDER_SDK 0
#endif

namespace biglybtc::engine {
struct VerifiedSocks4Capability final {
  explicit VerifiedSocks4Capability(std::shared_ptr<void> value, bool web = false) : library(std::move(value)), webSeeds(web) {}
  std::shared_ptr<void> library;
  bool webSeeds = false;
};

bool verifiedSocks4WebSeeds(const std::shared_ptr<const VerifiedSocks4Capability>& value) noexcept {
  return value && value->webSeeds;
}

std::shared_ptr<const VerifiedSocks4Capability> verifiedSocks4Capability() noexcept {
#if BIGLYBTC_SOCKS4_PROVIDER_SDK
  try {
    // This is the existing full ELEVEN-export original-export, catalog, actual
    // loaded DSO/hash/fd/inode/PT_LOAD proof. It transfers the SAME live handle.
    // Neither an extension header nor loading a second matching DSO can grant it.
    auto library = detail::managedPayloadCounterRuntimePin(true);
    if (!library) { return {}; }
    void* version = ::dlsym(library.get(), "_ZN10libtorrent7versionEv");
    void* marker = ::dlsym(library.get(), "biglybtc_socks4a_abi_v1");
    Dl_info provider{}, extension{};
    if (!version || !marker
        || version != ::dlsym(RTLD_DEFAULT, "_ZN10libtorrent7versionEv")
        || marker != ::dlsym(RTLD_DEFAULT, "biglybtc_socks4a_abi_v1")
        || !::dladdr(version, &provider) || !::dladdr(marker, &extension)
        || !provider.dli_fbase || provider.dli_fbase != extension.dli_fbase) { return {}; }
    const auto abi = reinterpret_cast<decltype(&libtorrent::biglybtc_socks4a_abi_v1)>(marker);
    if (abi() != 1) { return {}; }
    bool webSeeds = false;
#if defined(BIGLYBTC_SOCKS4A_WEBSEED_ABI) && BIGLYBTC_SOCKS4A_WEBSEED_ABI == 1
    // Independent optional authority. A marker exported by another mapped
    // image must not widen a valid base4a proof to webseed name resolution.
    void* web = ::dlsym(library.get(), "biglybtc_socks4a_webseed_abi_v1");
    Dl_info webImage{};
    if (web && web == ::dlsym(RTLD_DEFAULT, "biglybtc_socks4a_webseed_abi_v1")
        && ::dladdr(web, &webImage) && provider.dli_fbase == webImage.dli_fbase) {
      const auto webAbi = reinterpret_cast<decltype(&libtorrent::biglybtc_socks4a_webseed_abi_v1)>(web);
      webSeeds = webAbi() == 1;
    }
#endif
    return std::make_shared<VerifiedSocks4Capability>(std::move(library), webSeeds);
  } catch (...) { return {}; }
#else
  return {}; // Stock/old SDK has no admitted USERID/remote-name capability.
#endif
}
} // namespace biglybtc::engine
