# P06-26 Original database-value lifecycle

Status: implemented bounded lifecycle; all three fresh toolchains pass.
Evidence: migration/evidence/P06-26-azureus-db-value/{matrix.json,review.md}. Source baseline chosen before isolated implementation:
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Source corpus was frozen independently
before native comparison:7344 observations,6767 semantic +76 alias-policy +249
getter-contract +252 diagnostic. Only6767 semantic rows receive native equality
credit; the other577 remain source-only. Original class/interfaces are unadapted.

Implement pure C++23 owned DB values with exact create/receive/relay/deletion,
creation/store clocks and getter/narrowing rules. Deletion consumes two clock
reads and replaces sender with originator. DB replication frequency uses the
unsigned high nibble except default-1; qualified transport wire frequency keeps
its separate signed shift. No production wall clock, Qt, crypto, DB map, expiry,
persistence, routing or network activation is introduced.

Immutable private payload/contact handles copy caller ingress, share owned
internal snapshots, and keep borrowed views alive through retained handles.
The explicit64KiB native payload cap, moved-from state and driver limits are
native policy tests, not fabricated original-source rejections. Mutable Java
array/contact aliases and arbitrary custom getter callbacks remain separate.

Qualify exact6767 semantic rows plus ownership/clock/CLI boundaries and unchanged
value3644/node87230 fixtures on three fresh persistent GCC16/Clang24/ASan+UBSan+LSan
builds (seven CTest rows). Preserve7344 frozen files and all original helper/fence
classes, actual tested sources/Git blobs, failures and original Java HEAD/index.
Use generated data only; no original running program/live profile access.

Native implementation: `337abc2a73f649771c3cb5223c5e044d180372da`; all279 qualified inputs and one recovery source match their Git blobs.
