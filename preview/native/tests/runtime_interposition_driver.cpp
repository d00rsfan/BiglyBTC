#include "persistent_session.hpp"

#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

int main() {
  using namespace biglybtc::engine;
  const auto* preload = std::getenv("LD_PRELOAD");
  if (!preload || !*preload) { return 1; }
  void* provider = ::dlopen("libtorrent-rasterbar.so.2.0", RTLD_NOW | RTLD_NOLOAD);
  if (!provider) { return 2; }
  void* actual = ::dlsym(provider, "_ZN10libtorrent7versionEv");
  void* interposed = ::dlsym(RTLD_DEFAULT, "_ZN10libtorrent7versionEv");
  if (!actual || !interposed || actual == interposed) { ::dlclose(provider); return 3; }
  // Removing the environment string does not unload the real preloaded export.
  // This reaches the dynamic-export comparison rather than the cheap env gate.
  if (::unsetenv("LD_PRELOAD") || std::getenv("LD_PRELOAD")) { ::dlclose(provider); return 4; }
  const auto result = PersistentSession::create({1, 2, 3});
  ::dlclose(provider);
  if (result || result.error() != AccountingError::DependencyUnqualified) { return 5; }
  std::cout << "Already mapped interposed original export rejected after env removal\n";
  return 0;
}
