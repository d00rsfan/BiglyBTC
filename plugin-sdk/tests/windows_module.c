// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin.h"
#include <stdlib.h>

static bt_status create(const bt_plugin_options_v1 *options, void **instance) {
  (void)options;
  *instance = calloc(1, sizeof(int));
  return *instance ? BT_OK : BT_FAILED;
}
static bt_status start(void *instance) {
  *(int *)instance = 1;
  return BT_OK;
}
static bt_status call(void *instance, uint32_t operation, const void *request,
    uint32_t request_size, void *response, uint32_t response_size) {
  (void)request;
  (void)request_size;
  if (!*(int *)instance || operation != 7 || !response || response_size != sizeof(uint32_t)) {
    return BT_INVALID;
  }
  *(uint32_t *)response = 42;
  return BT_OK;
}
static void stop(void *instance) { *(int *)instance = 0; }
static void destroy(void *instance) { free(instance); }
static const bt_plugin_v1 api = {
  sizeof(bt_plugin_v1), BT_PLUGIN_ABI_V1, "fixture", "0.1.0", BT_CAP_LOCATION,
  create, start, call, stop, destroy
};

BT_PLUGIN_EXPORT const bt_plugin_v1 *biglybtc_plugin_entry(uint32_t version) {
  return version == BT_PLUGIN_ABI_V1 ? &api : NULL;
}
