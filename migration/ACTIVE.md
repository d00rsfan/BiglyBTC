# Migration status — 2026-10-27

Versioned and padded verified existing-data seeding at native `a2f050e27779c3b949db6f144f978ae0cc542a44`: gcc 9 suites, clang 9 suites, san 9 suites pass; 5 new actual original-client cases are recorded.

Pure-v2 aligned and nonaligned-tail payloads and padded hybrid payloads selected as V1 or V2 now use the same guarded existing-data verification. Every real file must be selected, complete and linear, with read-only descriptors and unchanged identity/stat proofs. Declared padding and the sole authenticated pure-v2 engine tail remain virtual and unselected; a poisoned pad-path sentinel cannot affect hashing. The exact original/engine projection, same-admission actual full-piece check, queue hold, sparse release CAS and legacy defaults remain intact. The disk worker also keeps the network event loop alive through shutdown/drain and returns consumed job captures to that loop for destruction; this addresses the reproduced late torrent teardown and final-owner thread ordering.

Exactly one authorized milestone remains in this compatibility section: authenticated existing custom save roots (batch10). Complete it, then STOP. Do not start the next chapter.

Allocated zero-fill lifecycle, skipped/linked/nonlinear existing-data seeding, arbitrary layouts and the recorded original empty-first v2/hybrid failures remain open. These nonempty fixtures do not waive those original boundaries. All CF01–CF08 remain open. Evidence: [batch9](evidence/20261027-compatibility-batch9/verification.json).

Native main only; three aggregate compiler jobs; existing caches/providers reused. The running original client, real profile/payloads, original ordinary HEAD/index and qualified dist are preserved. No system installation or .deb promotion. Durable checkpoint: `build/compatibility-20261026/checkpoint.json`.
