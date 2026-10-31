#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 2 || !std::getenv("LD_PRELOAD") || !libtorrent::version()) { return 1; }
  const bool owner = std::string_view(argv[1]) == "owner";
  if (!owner && std::string_view(argv[1]) != "torrents") { return 2; }
  const char* name = owner ? "biglybtc_private_i2p_owner_abi_v1" : "biglybtc_private_i2p_torrents_abi_v1";
  const unsigned expected = owner ? 0x42494f01U : 0x42495401U;
  void* module = ::dlopen("libtorrent-rasterbar.so.2.0", RTLD_NOW | RTLD_NOLOAD);
  if (!module) { return 3; }
  void* actual = ::dlsym(module, name);
  void* foreign = ::dlsym(RTLD_DEFAULT, name);
  Dl_info a{}, b{};
  if (!actual || !foreign || actual == foreign || !::dladdr(actual, &a) || !::dladdr(foreign, &b)
      || a.dli_fbase == b.dli_fbase) { ::dlclose(module); return 4; }
  using Marker = unsigned (*)() noexcept;
  if (reinterpret_cast<Marker>(actual)() != expected || reinterpret_cast<Marker>(foreign)() != expected) {
    ::dlclose(module); return 5;
  }
  if (::unsetenv("LD_PRELOAD") || std::getenv("LD_PRELOAD")) { ::dlclose(module); return 6; }
  const bool admitted = biglybtc::engine::detail::persistentRuntimeQualified();
  const auto retained = biglybtc::engine::detail::mainlineRoutingRuntimePin();
  ::dlclose(module);
  if (admitted || retained) { return 7; }
  std::cout << "same_value_private_marker=1 all34_rejected=1 after_env_removed=1\n";
  return 0;
}
