# Initial bounded file-policy review

Reviewer: Codex, self review only, 2026-10-17. Native implementation: `9c1ab4828116590d9cfe8933615eddebd387e7a0`. Upstream https://github.com/BiglySoftware/BiglyBT.git baseline/candidate `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; initial port, no verified integrated A.

Read GlobalManagerImpl compact/legacy priority persistence and DiskManagerUtil loading/storing. Native preserves Java Long.intValue and signed priority/skip semantics, sparse normal defaults and unmodified source encodings. Compact range expansion is bounded and conflicting overlaps reject activation. Non-skipped linear priorities retain distinct ordering up to seven engine ranks; positive per-torrent byte-rate limits are mapped without changing saved fields.

Evidence: four unchanged original loader sets, native boundary units, genuine Java-written v1/v2/hybrid -3/5 priorities with32768/65536 byte limits, same-profile continuation and Java reopening, actual Qt v2 policy workflow, and five rejected policies with all profile/cache/source-payload bytes unchanged and no peer connection. [GCC37 matrix](../../evidence/P03-03/gcc-full-37-multifile-results.json), [full test output](../../evidence/P03-03/gcc-full-37-last-test.txt).

Pending required behavior: skipped-file storage, more than seven priority ranks, global/disabled-rate policies, exact broader piece-selection behavior, malformed legacy compatibility classification and remaining compiler/platform matrices. Rejecting unsupported input is an intermediate explicit limitation, not an accepted feature retirement. Implemented/unverified; first_ported/integrated hashes remain null.
