# P10-03 Cache-aware original RPC profile data

Status: bounded provider implemented and qualified. Read persisted IDs/counter from the same borrowed profile
lease used by UI/engine operations. Do not acquire a second profile lock.

Retain the existing read-only library projection and share the already decoded
active/cache.dat document for indexed plugin attribute access. Last hash wins;
any cached attribute dictionary, including empty, bypasses primary state reads.
Fallback uses original active/<HASH>.dat and backup handling without beginUpdate
or cache invalidation. Global counter comes from biglybt.config under the actual
external plugin-folder prefix. Archive IDs come from ordered dlarchive.config
stubs and attr maps; they participate in original uniqueness checks too.

Return owned values and diagnostics. Missing/malformed IDs remain unavailable,
never manufactured. No ID repair/allocation or file write is permitted here.
Archive restoration/UI, full normal runtime registration/magnet ordering,
HTTP/real RPC methods and durable multi-file ID/counter journal remain required.
Test only fresh generated profiles, with source-built original pure getters and
complete byte/inode preservation checks, including cache-only/missing-primary.

Allowed implementation paths: native/adapters/{rpc_profile,library_view,
plugin_attributes}.{hpp,cpp}, their CMake/test consumers and task-local fixtures,
tools/evidence. Source baseline is original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f and xmwebui
c9ab8f7cd62b2874f7385772992c0288a224e85d. Sources are read-only. Actual config
filename is biglybt.config; installation folder names and runtime IDs have
different prefixes. dlarchive.config uses ordered stubs with attr dictionaries.

Acceptance: python3 migration/tools/rpc_profile_matrix.py. Fresh serial builds,
two compiler jobs at nice5: GCC16, Clang24, ASan/UBSan with LSan enabled. Replay
38 getter-backed filesystem cases, native unsafe-path/backup/100001-entry
boundaries, original attributes/settings/routing-cache/core/document/library/
gzip regressions and retained original Java cached-library reads. Every provider
fixture repeats under one held lease, checks early cancellation and preserves
bytes/inodes/symlink targets. Cache-only cases include missing, corrupt, symbolic
and FIFO primaries to detect unnecessary file access. Keep evidence hashes/logs.

The reference reuses independently qualified exact getter bodies with supplied
maps/defaults/monitors. Its lazy filesystem/recovery adapter is test scaffolding;
it does not run full original startup, CachedStateWrapper construction, active
registration or archived DownloadStubImpl restoration. Three whitelisted
metadata-only seed sets come from retained Java-generated v1/v2/hybrid profiles;
keystores, certificates and payloads are excluded. Native-only path/limit tests
describe native safety policy, not extra Java acceptance claims.

All 12 selected CTest rows pass on GCC16, Clang24 and ASan/UBSan/LSan. Sandbox
LSan initialization failed because process tracing was restricted; the exact
built sanitizer suite passed under the auto-reviewed process permissions with
leak detection enabled. Both attempts are retained; no sysctl, sanitizer
suppression or live application setting was changed. See evidence/P10-03-rpc-profile.
