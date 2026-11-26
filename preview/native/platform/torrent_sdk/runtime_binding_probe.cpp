// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../engine/persistent_runtime_guard.hpp"
#include "../../services/i2p_provider/owner_runtime_guard.hpp"

#include <libtorrent/version.hpp>

#include <iostream>
#include <string_view>

int main() {
  using namespace biglybtc::engine::detail;
  const auto mainline = mainlineRoutingRuntimePin();
  if (!std::string_view(libtorrent::version()).starts_with("2.0.") || !persistentRuntimeQualified() || !managedPayloadCounterRuntimePin(true)
      || !mainline || !persistentRuntimeSymbol(mainline.get(), "biglybtc_mainline_port_abi_v1")
      || persistentRuntimeSymbol(mainline.get(), "biglybtc_nonexistent_probe_symbol") || !mixedI2pTransportRuntimePin()
      || !biglybtc::services::i2p::provider::privateOwnerRuntimePin()) {
    std::cerr << "The native engine refused the bound SDK or one of its required roles\n";
    return 1;
  }
  std::cout << "The application runtime qualified the loaded daily image and its required roles\n";
  return 0;
}
