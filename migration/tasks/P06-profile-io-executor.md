# Keep profile I/O off the DHT network owner

State: bounded executor implementation qualified; broader kernel-stall/resource/performance acceptance remains open before public activation.

Source `f9c72f14be1494b6d75b38288f9eff256a1a878b` qualifies bounded profile I/O off the DHT network owner and explicit I2CP hash lookup/connect/scrape. Fresh GCC17/Clang12/leak-enabled ASan+UBSan12 suites, private-runtime17 and actual official/modified Java2signing families/10frames per reference/direction pass. [Review](evidence/20261029-profile-io-i2cp-tracker/REVIEW.md).

One private unchanged core Executor per service bounds queued/active/ready I/O to8slots and held frames to64. Same-OFD cooperative source access/revisions, source-gate-before-version-mutex ordering and one-shot actor acknowledgement protect current profile/request authority. Reads/reservations/fsync execute on the worker; network continuations perform no filesystem call, source-gate acquisition or future wait. Close advances socket/commands/deadlines before external worker join. Unknown fields, filenames, version widths, CAS and indeterminate-publication semantics remain unchanged.

Actual1MiB read/fsync plus held-worker handoff tests qualify owner progress, deadlines, cancellation, rebind, source replacement and capacity reclamation. They do not measure a blocked kernel syscall: an entered source gate/syscall can still delay full external join. Full latency/resource benchmarks and public performance acceptance remain open. Hostile noncooperative pathname swaps remain outside the cooperative-lock contract. No unrelated Java projection/provider replay was needed.
