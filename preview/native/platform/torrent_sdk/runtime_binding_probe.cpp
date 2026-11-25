// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../engine/persistent_runtime_guard.hpp"

#include <libtorrent/version.hpp>

#include <iostream>
#include <string_view>

int main() {
  using namespace biglybtc::engine::detail;
  if (!std::string_view(libtorrent::version()).starts_with("2.0.") || !persistentRuntimeQualified() || !managedPayloadCounterRuntimePin(true)
      || !mainlineRoutingRuntimePin() || !mixedI2pTransportRuntimePin()) {
    std::cerr << "The native engine refused the bound SDK or one of its required roles\n";
    return 1;
  }
  std::cout << "The application runtime qualified the loaded daily image and its required roles\n";
  return 0;
}
