# Active migration work — 2026-10-27

Compatibility batch7 qualifies bounded file-operation receipt reuse at native `dad74e36931da36a90de2c65aed0130e6fae26f6`: three affected suites pass in each GCC, Clang and leak-enabled ASan/UBSan lane. Nine actual unequal-size/sparse payload-read cases and two cold-recovery cases per lane retain strict content checks, journal order and descriptor cleanup.

Only the pre-journal validation traverses backwards, retaining the first eight proofs consumed by forward execution. The file vector, journal, mutations, notifications, recovery, receipt capacity and named/held-FD/ctime checks remain unchanged. Actual counters measure returned userspace-read bytes; CPU and disk traffic are not measured.

[Batch7 evidence](evidence/20261027-compatibility-batch7/verification.json). All CF01–CF08 remain open; prior original/runtime/protocol evidence and dist remain unchanged.

Continue remaining [compatibility work](tasks/COMPATIBILITY-FOLLOWUPS.md) autonomously. Next: existing-only managed storage as a prerequisite for fully verified existing-file seeding; do not expose assumed-complete/allocated modes without their actual lifecycle. Previous STOP boundaries are historical. Durable state: `build/compatibility-20261026/checkpoint.json`. Native main only; at most three compiler jobs. Batch checks. Preserve the running original, real profile/payloads and ordinary HEAD/index; no system installation.
