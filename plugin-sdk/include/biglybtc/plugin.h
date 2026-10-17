/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BIGLYBTC_PLUGIN_H
#define BIGLYBTC_PLUGIN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_PLUGIN_ABI_V1 UINT32_C(1)
#define BT_CAP_LOCATION UINT64_C(1)
#define BT_CAP_SCHEDULER UINT64_C(2)
#define BT_PLUGIN_EXPORT __attribute__((visibility("default")))
typedef int32_t bt_status;
#define BT_OK INT32_C(0)
#define BT_NOT_FOUND INT32_C(1)
#define BT_INVALID INT32_C(2)
#define BT_UNSUPPORTED INT32_C(3)
#define BT_IO INT32_C(4)
#define BT_BUFFER_SMALL INT32_C(5)
#define BT_FAILED INT32_C(6)
typedef struct bt_buffer_v1 {
  uint8_t *data; /* host allocated */
  uint32_t capacity;
  uint32_t size; /* required length on BT_BUFFER_SMALL, written length on BT_OK */
} bt_buffer_v1;
/* Host-owned locale caches outlive plugin modules. Country-name callback writes
 * UTF-8 plus NUL; size includes that terminator. Optional: absent => DB English.
 * Same owner thread; may not re-enter/unload this plugin or retain arguments. */
typedef bt_status (*bt_country_name_fn)(void *context, const char *iso3166,
    const char *locale, bt_buffer_v1 *result);
/* All calls are serialized by one host owner, including stop/destroy. No
 * background callbacks, exceptions, STL/Qt objects or allocator ownership cross
 * this boundary. Strings are UTF-8, NUL terminated, with explicit fixed bounds.
 * All pointers are borrowed for the duration of the call unless stated below.
 * This is an experimental Linux LP64 ABI; production stability is not promised. */
typedef struct bt_plugin_options_v1 {
  uint32_t size;
  uint32_t abi;
  int32_t plugin_directory_fd; /* read-only original plugin directory, host-owned */
  uint32_t reserved;
  const char *java_plugin_version; /* <= 64 bytes, copied by create */
  void *host_context; /* must outlive stop/destroy */
  bt_country_name_fn country_name;
} bt_plugin_options_v1;
typedef struct bt_plugin_v1 {
  uint32_t size;
  uint32_t abi;
  const char *id; /* static module-owned strings, valid until dlclose */
  const char *version;
  uint64_t capabilities;
  bt_status (*create)(const bt_plugin_options_v1 *, void **instance);
  bt_status (*start)(void *instance);
  bt_status (*call)(void *instance, uint32_t operation,
      const void *request, uint32_t request_size, void *response, uint32_t response_size);
  void (*stop)(void *instance); /* idempotent, drains work before returning */
  void (*destroy)(void *instance); /* must also work without successful start */
} bt_plugin_v1;
typedef const bt_plugin_v1 *(*bt_plugin_entry_fn)(uint32_t requested_abi);
/* The sole required exported symbol. Query has no side effects. */
BT_PLUGIN_EXPORT const bt_plugin_v1 *biglybtc_plugin_entry(uint32_t requested_abi);

#define BT_LOCATION_LOOKUP UINT32_C(0x1001)
#define BT_LOCATION_FLAG UINT32_C(0x1002)
typedef struct bt_location_request_v1 {
  uint32_t family; /* 4 or 6; numeric addresses only, never DNS */
  uint8_t address[16]; /* network byte order; IPv4 uses first four bytes */
  char locale[64]; /* ICU/Java language[_REGION], empty means English */
} bt_location_request_v1;
typedef struct bt_location_result_v1 {
  char iso3166[3];
  char country[253]; /* localized UTF-8 country, empty if unknown */
} bt_location_result_v1;
typedef struct bt_flag_request_v1 {
  char iso3166[3];
  uint8_t reserved;
  int32_t size_index; /* original: zero = 18x12, every other index = 25x15 */
} bt_flag_request_v1;
#ifdef __cplusplus
}
#endif
#endif
