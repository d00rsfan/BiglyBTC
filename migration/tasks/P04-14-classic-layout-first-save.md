# P04-14 Save classic columns in a profile without tables.config

State: bounded implementation qualified, committed, pushed and tagged.
Source `8efe56e4c0a4c46b8cdcb51035364db938494bb5`, metadata
`8127336eada41e5795d6fdadaa42eea0ff555ec1`, annotated tag
`milestone-classic-layout-first-save-20261018` (object
`8edd4562ed378a8ae1b1da49552aa527cf9ceab8`). All15 tested/committed files
match exact hashes. GCC44 units, Clang4 suites/195 cases, leak-enabled ASan/UBSan
5 suites including21 Qt window/worker rows, and actual100/150/200% smoke with5
rows per scale pass. Existing4 real write faults/2 successful retries plus fenced
original Java recovery pass on all3 lanes. Hosted desktop39135512836 independently
confirms44 units/21 Qt rows/5 smoke rows; SDK39135512901 passes. Durable dist103
files and82 unchanged private runtimes verify, exact installed RUNPATH replay
matches full ELF bytes, and previous-dist is retained. Source and tag remote
identities verify. No test, fixture, compiler flag or deadline relaxed.

Owner/integrator: root; three parallel agents own low-level creation, independent
storage tests, and the preference adapter/tests respectively.

The user authorized continuation. This closes the missing-file exclusion from
P04-13 for explicit Save only. Read, opening a window, resize, Reset, close and
Reload never create a configuration. Development uses generated isolated
profiles; the running Java application and its live profile/payload are excluded.

Source baseline: original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, TableColumnManager table-map
read/save/reset and FileUtil resilient dictionary format. The native parent is
`2501968f62d4ec7e70b093c2186a31fcf2ede518`; P04-13 implementation is
`ba03f2c6a080f856c0d8808b2b4239846dd27733`. This extends native persistence;
it does not change or claim equality with the original Java manager's layout
registration, sorting, or full save/reset runtime.

Contract: a successful held-session read observing all three tables.config,
tables.config.bak and tables.config.saving absent authorizes an explicit first
save. No read, failed read, recovery read, old session or failed save provides
that authorization. The worker owns that state; the GUI carries no file path or
write baseline. First save publishes only BiglyBTC.ClassicColumns.v1, with the
same strict P04-13 schema/limits. Success advances to the usual Primary baseline
for subsequent saves. Failure clears save admission until Reload.

Creation validates the held lease, safe basename, bounded complete dictionary
and absence of all three candidate pathnames. All success objects allocate and
parse before writing. It writes and synchronizes an exclusively created .saving
owned descriptor, checks the staging identity, rechecks Primary/Backup absence,
then uses Linux renameat2(RENAME_NOREPLACE) and synchronizes the directory.
It never replaces a newly appeared Primary, invents a Backup, follows a symlink,
or deletes an existing recovery pathname. Failure can leave a recoverable .saving;
failure after publication says that the Primary may already be present. This is
not a transaction against noncooperating writers replacing other pathnames.

Cancellation is observed before the synchronous creation begins; its actual
result is returned once started. Save acknowledgement never changes headers,
rows, selection, filter or details. Newer local edits remain unsaved. Existing
Primary writes and all recovery refusal rules remain qualified separately.

Qualification: fresh generated normal/restart/two-save and late-conflict cases;
existing regular/malformed/empty/symlink/directory candidate preservation;
invalid input/lease and pre-write cancellation; deterministic post-sync and
post-publication failure observations; existing file-write regression suites;
GCC16 Release, Clang24, leak-enabled ASan/UBSan, and actual preview smoke.
Qt6.12.0 and the pinned C++23 toolchains/flags remain unchanged. Root owns shared
CMake, Bridge/UI integration, evidence, commits and publication. No system install
or normal-profile activation. Evidence belongs under
`migration/evidence/20261018-classic-layout-first-save/` and build work under the
durable sibling `build/classic-layout-first-save-20261018/`.

Implementation source hashes, test outcomes and immutable implementation commit
will be recorded after actual qualification; no release or whole-feature
completion claim follows from this task.

Final evidence: local-verification.json, hosted-verification.json, source-commit.json,
publication.json, dist-promotion.json and five independent source-review records
under the task evidence directory. Original Java HEAD/index verified unchanged.
Next user-priority implementation: P04-15 Tools > Statistics / Ctrl+5.
