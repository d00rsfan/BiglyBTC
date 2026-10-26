#include "network_policy.hpp"
#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>

#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using namespace biglybtc;
using Marker = unsigned (*)() noexcept;
std::string bytes(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
bool strictWithoutProof() {
  const auto raw = "d" + bytes("Proxy.Data.Enable") + "i1e"
      + bytes("Proxy.Data.SOCKS.version") + bytes("V4a")
      + bytes("Proxy.Host") + bytes("127.0.0.1") + bytes("Proxy.Port") + bytes("1080")
      + bytes("Proxy.Username") + bytes("owned-user")
      + bytes("peercontrol.udp.public.enable") + "i0e" + bytes("Server Enable UDP") + "i0e"
      + bytes("Peer Source Selection Default.DHT") + "i0ee";
  const auto document = core::Document::parse(raw);
  if (!document) { return false; }
  const auto parsed = engine::readNetworkPolicy(&*document, {engine::NetworkMode::Normal});
  return !parsed && parsed.error().code == engine::NetworkPolicyErrorCode::Unsupported
      && document->serialize() == raw;
}
}

int main(int argc, char** argv) {
  if (argc != 2 || !libtorrent::version() || !strictWithoutProof()) { return 1; }
  const std::string_view mode(argv[1]);
  if (mode == "absent") {
    if (biglybtc::engine::verifiedSocks4Capability()) { return 2; }
    std::cout << "capability=0 default-or-old-provider-denied=1 no-owner-created=1\n";
    return 0;
  }
  if (mode == "present" || mode == "present-webseed") {
    auto pin = biglybtc::engine::detail::managedPayloadCounterRuntimePin(true);
    if (!pin) { return 3; }
    void* own = ::dlsym(pin.get(), "biglybtc_socks4a_abi_v1");
    void* version = ::dlsym(pin.get(), "_ZN10libtorrent7versionEv");
    Dl_info marker{}, provider{};
    if (!own || !version || own != ::dlsym(RTLD_DEFAULT, "biglybtc_socks4a_abi_v1")
        || !::dladdr(own, &marker) || !::dladdr(version, &provider)
        || !provider.dli_fbase || marker.dli_fbase != provider.dli_fbase
        || reinterpret_cast<Marker>(own)() != 1) { return 4; }
    const auto capability = biglybtc::engine::verifiedSocks4Capability();
    if (!capability || engine::verifiedSocks4WebSeeds(capability) != (mode == "present-webseed")) { return 5; }
    std::cout << "full-eleven-export-proof=1 marker-same-image=1 capability=1 no-owner-created=1\n";
    return 0;
  }
  if (mode == "foreign-webseed") {
    const auto* preload = std::getenv("LD_PRELOAD");
    if (!preload || !*preload || engine::verifiedSocks4Capability()) { return 12; }
    if (::unsetenv("LD_PRELOAD") || std::getenv("LD_PRELOAD")) { return 13; }
    auto pin = engine::detail::managedPayloadCounterRuntimePin(true);
    if (!pin) { return 14; }
    void* own = ::dlsym(pin.get(), "biglybtc_socks4a_webseed_abi_v1");
    void* foreign = ::dlsym(RTLD_DEFAULT, "biglybtc_socks4a_webseed_abi_v1");
    void* calls = ::dlsym(RTLD_DEFAULT, "biglybtc_foreign_webseed_marker_calls");
    Dl_info a{}, b{};
    if (!own || !foreign || !calls || own == foreign || !::dladdr(own, &a) || !::dladdr(foreign, &b)
        || !a.dli_fbase || !b.dli_fbase || a.dli_fbase == b.dli_fbase
        || reinterpret_cast<Marker>(own)() != 1 || reinterpret_cast<Marker>(calls)() != 0) { return 15; }
    const auto capability = engine::verifiedSocks4Capability();
    if (!capability || engine::verifiedSocks4WebSeeds(capability)
        || reinterpret_cast<Marker>(calls)() != 0) { return 16; }
    std::cout << "full-eleven-export-proof=1 base4a-capability=1 foreign-webseed-image=1 webseed-capability=0 foreign-not-invoked-by-guard=1 no-owner-created=1\n";
    return 0;
  }
  if (mode != "foreign") { return 6; }
  const auto* preload = std::getenv("LD_PRELOAD");
  if (!preload || !*preload || biglybtc::engine::verifiedSocks4Capability()) { return 7; }
  // Keep the actual preloaded module mapped, then remove only the environment
  // gate. Eleven ORIGINAL provider exports still have to pass the full proof.
  if (::unsetenv("LD_PRELOAD") || std::getenv("LD_PRELOAD")) { return 8; }
  auto pin = biglybtc::engine::detail::managedPayloadCounterRuntimePin(true);
  if (!pin) { return 9; } // Denial at an earlier provider gate is NOT credit.
  void* own = ::dlsym(pin.get(), "biglybtc_socks4a_abi_v1");
  void* foreign = ::dlsym(RTLD_DEFAULT, "biglybtc_socks4a_abi_v1");
  void* calls = ::dlsym(RTLD_DEFAULT, "biglybtc_foreign_socks4_marker_calls");
  Dl_info a{}, b{};
  if (!own || !foreign || !calls || own == foreign || !::dladdr(own, &a) || !::dladdr(foreign, &b)
      || !a.dli_fbase || !b.dli_fbase || a.dli_fbase == b.dli_fbase
      || reinterpret_cast<Marker>(own)() != 1 || reinterpret_cast<Marker>(foreign)() != 1
      || reinterpret_cast<Marker>(calls)() != 1) { return 10; }
  if (biglybtc::engine::verifiedSocks4Capability() || reinterpret_cast<Marker>(calls)() != 1) { return 11; }
  std::cout << "full-eleven-export-proof=1 foreign-abi=1 mapped-after-env-removed=1 capability=0 foreign-not-invoked-by-guard=1 no-owner-created=1\n";
  return 0;
}
