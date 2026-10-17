#include "biglybtc/plugin.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
static void event(int fd, char value) {
  int output = openat(fd, "events", O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (output >= 0) { if (write(output, &value, 1) != 1) { abort(); } close(output); }
}
static bt_status create(const bt_plugin_options_v1 *options, void **instance) {
  int *value = malloc(sizeof(int));
  if (!value) { return BT_FAILED; }
  *value = dup(options->plugin_directory_fd);
  *instance = value;
  event(*value, 'C');
  return BT_TEST_MODE == 5 ? BT_FAILED : BT_OK;
}
static bt_status start(void *instance) {
  event(*(int *)instance, 'A');
  return BT_TEST_MODE == 6 ? BT_FAILED : BT_OK;
}
static bt_status call(void *instance, uint32_t operation, const void *request,
    uint32_t request_size, void *response, uint32_t response_size) {
  (void)operation; (void)request; (void)request_size; (void)response; (void)response_size;
  event(*(int *)instance, 'Q'); return BT_UNSUPPORTED;
}
static void stop(void *instance) { event(*(int *)instance, 'S'); }
static void destroy(void *instance) {
  event(*(int *)instance, 'D'); close(*(int *)instance); free(instance);
}
static const bt_plugin_v1 api = {
  BT_TEST_MODE == 1 ? 0 : sizeof(bt_plugin_v1), BT_TEST_MODE == 2 ? 99 : 1,
  BT_TEST_MODE == 3 ? "other" : "fixture", "0.1.0", BT_TEST_MODE == 4 ? 0 : BT_CAP_LOCATION,
  create, start, call, stop, destroy
};
BT_PLUGIN_EXPORT const bt_plugin_v1 *biglybtc_plugin_entry(uint32_t version) {
  return version == 1 ? &api : NULL;
}
