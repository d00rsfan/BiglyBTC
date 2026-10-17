#include "biglybtc/plugin.h"
_Static_assert(sizeof(bt_location_request_v1) == 84, "LP64 location request ABI");
_Static_assert(sizeof(bt_location_result_v1) == 256, "location result ABI");
_Static_assert(sizeof(bt_flag_request_v1) == 8, "flag request ABI");
_Static_assert(sizeof(bt_buffer_v1) == 16, "LP64 buffer ABI");
_Static_assert(sizeof(bt_plugin_v1) == 72, "LP64 plugin ABI");
_Static_assert(sizeof(bt_plugin_options_v1) == 40, "LP64 options ABI");
int main(void) { return BT_PLUGIN_ABI_V1 != 1; }
