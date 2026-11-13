# Diagnostics observed during the static desktop integration build

GCC16.0.1/libstdc++16 Release compilation emitted two warnings outside search:

- `app/subscription_acquisition.cpp:129`, `settleLegacy`: inlined vector/string
  destruction reports `-Wfree-nonheap-object` (nonzero offset32).
- `app/category_bridge.inc:90` through `session_bridge.cpp:178 take` and
  `adapters/category_store.hpp:51 CategoryCommit`: `-Wmaybe-uninitialized`
  for the default-initialized `changed` field while moving an expected value.

No confirmed runtime fault or compiler false-positive conclusion yet. Keep the
diagnostics and source positions for a focused follow-up; do not silence them
globally or infer that search introduced them. RSS/subscriptions remain low
priority. Inspect category handling before a future release/profile edit claim.
The full compiler trace remains local in chapter-build.log.
