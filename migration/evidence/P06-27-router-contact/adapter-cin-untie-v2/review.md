# Qualified bounded router-contact lifecycle

The C++23 immutable router-contact record is qualified against original Java
commit `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Three fresh GCC16 Release,
Clang24 Release and Clang24 ASAN+UBSAN+LSAN Debug lanes each pass ten CTests:
5,054 original semantic comparisons,258 native ownership/bounds assertions and
851 CLI policy observations, with retained DB-value/wire/node regressions.
The1,933 source-only alias/getter/seeded/diagnostic observations receive zero
native equality credit. Fourteen expected getter exceptions remain source facts.

Two earlier sanitizer DB CLI120s failures remain immutable historical evidence.
The separately reviewed one-line stdio adapter unties cin before batch input;
parser, engine, core semantics, response flushing and all test deadlines remain
unchanged. Its narrow benchmark does not establish a timeout cause or whole-client
performance improvement. The new three-lane qualification uses the amended
adapter and the original strict test boundaries, with leak detection enabled.

Root independently read the445-line standalone postvalidator and the actual
stage1 report. It reparses all ten LastTest sections in each lane and verifies
346 current source inputs, retained279+one approved P26 override, historical
335-file evidence,495 runtime inputs,72 helper classes and5,347 core classes.
Each lane has13 owned command receipts and13 quiescent snapshots, plus the two
preparation snapshots. Native binaries/archives, actual compiler dependencies,
semantic caches, strict C++23 warning flags and ELF runtime maps are verified.
One live manager TracerPid0 observation binds the exact owned launch identity.
Exited CTest leaders have root-attested untraced launch-path evidence only.

Actual implementation checkpoint:
`20520f590610966047dfc59e01f4ff4422bfd1e9`.
The separate actual-Git stage matches345 qualified inputs to same-path Git blobs
and one ignored historical pre-adapter file to its byte-identical archived Git
path. The compiled current adapter is separately matched at its original path.
No future or self-referential hash is used. The earlier partial implementation
`691376ed23bf65025800c9385dfaea774ed1462c` remains in native history.

This component is implemented as a bounded standalone record. Mutable aliases,
arbitrary attachment/getter/diagnostic behavior, routing buckets/timers/bootstrap,
transport-result application, DB/persistence and normal client integration remain
unported. Wider source first-port/integration hashes stay null. This evidence
does not qualify a full router, plugin, application, release or migration phase.
