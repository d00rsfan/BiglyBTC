/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BIGLYBTC_SCHEDULER_H
#define BIGLYBTC_SCHEDULER_H
#include "plugin.h"
#define BT_SCHEDULER_EVALUATE UINT32_C(0x2001)
#define BT_TORRENT_COMPLETE UINT32_C(1)
#define BT_TORRENT_FORCED UINT32_C(2)
#define BT_TORRENT_PAUSED UINT32_C(4)
#define BT_TORRENT_OWNED_PAUSE UINT32_C(8)
#define BT_SCHEDULER_ENABLED UINT32_C(1)
#define BT_SCHEDULER_OVERRIDE_FORCED UINT32_C(2)
#define BT_SCHEDULER_NONDEFAULT_UPLOAD UINT32_C(4)
#define BT_SCHEDULER_NONDEFAULT_DOWNLOAD UINT32_C(8)
#define BT_SCHEDULER_UPLOAD_CHANGED UINT32_C(16)
#define BT_SCHEDULER_DOWNLOAD_CHANGED UINT32_C(32)
#define BT_SCHEDULER_PAUSE UINT32_C(1)
#define BT_SCHEDULER_RESUME UINT32_C(2)
#define BT_SCHEDULER_RESTART UINT32_C(3)
#define BT_SCHEDULER_STOP_REASON UINT32_C(4)
typedef struct bt_scheduler_torrent_v1 {
  uint64_t token; /* opaque stable host ID, returned in actions */
  uint32_t state; /* original Download.ST_* numeric 1..9 */
  uint32_t flags;
  const char *const *memberships; /* explicit original category or tag membership */
  uint32_t membership_count;
  uint32_t reserved;
} bt_scheduler_torrent_v1;
typedef struct bt_scheduler_request_v1 {
  const uint8_t *xml; /* original SavedSchedules.xml, no normalization/write */
  uint32_t xml_size;
  uint32_t sunday_based_day;
  uint32_t minute;
  uint32_t flags;
  int32_t effective_upload; /* original KiB/s, zero unlimited */
  int32_t global_download;
  const bt_scheduler_torrent_v1 *torrents;
  uint32_t torrent_count;
  uint32_t reserved;
} bt_scheduler_request_v1;
typedef struct bt_scheduler_action_v1 {
  uint64_t token;
  uint32_t kind;
  uint32_t reserved;
} bt_scheduler_action_v1;
typedef struct bt_scheduler_result_v1 {
  bt_scheduler_action_v1 *actions; /* caller-owned, no changes applied by plugin */
  uint32_t action_capacity;
  uint32_t action_count; /* required count on BT_BUFFER_SMALL */
  uint32_t flags;
  int32_t effective_upload;
  int32_t global_download;
  uint32_t reserved;
} bt_scheduler_result_v1;
#endif
