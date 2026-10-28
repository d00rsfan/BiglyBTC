#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

int main() {
  using namespace biglybtc::engine::detail;
  const auto* preload = std::getenv("LD_PRELOAD");
  if (!preload || !*preload || !libtorrent::version()) { return 1; }
  void* provider = ::dlopen("libtorrent-rasterbar.so.2.0", RTLD_NOW | RTLD_NOLOAD);
  if (!provider) { return 2; }
  void* actual = ::dlsym(provider, "biglybtc_mainline_port_abi_v1");
  void* interposed = ::dlsym(RTLD_DEFAULT, "biglybtc_mainline_port_abi_v1");
  Dl_info original{}, foreign{};
  if (!actual || !interposed || actual == interposed || !::dladdr(actual, &original)
      || !::dladdr(interposed, &foreign) || original.dli_fbase == foreign.dli_fbase) {
    ::dlclose(provider); return 3;
  }
  using Marker = unsigned (*)() noexcept;
  if (reinterpret_cast<Marker>(actual)() != 1 || reinterpret_cast<Marker>(interposed)() != 1) {
    ::dlclose(provider); return 4;
  }
  // The false marker remains mapped. Genuine old proof must still succeed,
  // proving that rejection reaches the NEW export comparison, not env/version.
  if (::unsetenv("LD_PRELOAD") || std::getenv("LD_PRELOAD")) { ::dlclose(provider); return 5; }
  const bool genuineBase = persistentRuntimeQualified();
  const auto genuine15 = managedExtendedTrafficRuntimePin();
  const auto mainline20 = mainlinePortRuntimePin();
  ::dlclose(provider);
  if (!genuineBase || !genuine15 || mainline20) { return 6; }
  std::cout << "genuine6=1 genuine15=1 same_value_foreign20_rejected=1 after_env_removed=1\n";
  return 0;
}
