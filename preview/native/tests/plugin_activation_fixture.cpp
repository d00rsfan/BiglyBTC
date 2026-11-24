// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin.h"

#include <cstdint>

namespace {
int owner;
bt_status create(const bt_plugin_options_v1* options, void** instance) {
  if (!options || !instance || options->size != sizeof(*options) || options->abi != BT_PLUGIN_ABI_V1 ||
      options->plugin_directory_fd < 0) { return BT_INVALID; }
  *instance = &owner;
  return BT_OK;
}
bt_status start(void* instance) { return instance == &owner ? BT_OK : BT_INVALID; }
bt_status call(void*, std::uint32_t, const void*, std::uint32_t, void*, std::uint32_t) { return BT_UNSUPPORTED; }
void stop(void*) {}
void destroy(void*) {}
const bt_plugin_v1 api{sizeof(api), BT_PLUGIN_ABI_V1, "azlocprov", "0.2.0", BT_CAP_LOCATION,
    create, start, call, stop, destroy};
}
extern "C" BT_PLUGIN_EXPORT const bt_plugin_v1* biglybtc_plugin_entry(std::uint32_t abi) {
  return abi == BT_PLUGIN_ABI_V1 ? &api : nullptr;
}
