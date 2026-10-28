# P06-27 Original router-contact state

Status: bounded standalone native record implemented and qualified. Three fresh
GCC16, Clang24 and leak-enabled ASAN/UBSAN lanes each pass all ten tests using
the explicitly approved stdio adapter. Actual checkpoint
`20520f590610966047dfc59e01f4ff4422bfd1e9` contains all346 qualified inputs
(345same-path plus one exact historical archive); separate Git-blob validation
and provenance retain the earlier partial checkpoint. See
[qualification review](../evidence/P06-27-router-contact/adapter-cin-untie-v2/review.md).
Wider source first-port/integration hashes stay null, and normal router/DB/client
integration remains pending. No whole feature/phase or release completion.

Historical partial qualification (preserved): GCC/Clang passed ten tests while
two original sanitizer attempts passed nine and exceeded the unchanged DB CLI
120s boundary. Both failures remain in partial-results.json. Their cause was
unproven; later acceptance used a separately reviewed one-line stdio change and
three fresh lanes, without increasing any test deadline or disabling checks.

Historical source-freeze stage: Baseline is
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; component is
`transport.azureus_router_contact`, feature `F09.router_contact_lifecycle`.
The original reference compiled and all 6,987 observations froze successfully.
No native implementation or comparison is claimed at this stage.

First freeze the exact complete DHTRouterContactImpl class, both interfaces,
scripted clock and inert attachment/diagnostic context before native work.
Preserve the original nonstatic declaration, all nine fields, 26 method bodies,
constructor backpointer order, dynamic failure thresholds, partial mutations
on callback failures, zero first-alive sentinel and Java int/long wrapping.
Snapshots must call only the existing 14 pure getters and must not add a clock,
threshold, attachment callback or diagnostic observation.

The independently frozen source corpus contains 6,987 observations: 5,054 semantic, 144 alias-policy,
468 getter-contract, 1,017 harness-seeded boundary and 304 diagnostic. All 5,054 semantic rows have zero source exceptions and are eligible for the
subsequent native comparison. The other 1,933 rows have zero native equality
credit; fourteen expected source exceptions occur in getter contracts. Keep all
generated expected bytes immutable.

After the root grants the JVM slot: verify/promote the proposed final tools,
source template, raw source audit and unchanged plan; preserve all pre-execution
inputs; run the explicit builder once and freeze every group using its verified
reference. Use JDK17 release8, nice5/two CPU, own helper-only classpath and the
unchanged ReferenceFence. Save first failures, full source/log/classes/reference
and partial outputs persistently. Never initialize ordinary Router/RouterNode,
Control, real UDP contact, SystemTime provider, logger/core, profile or network.

Before later native implementation, record a separate owned-ID/attachment,
missing-attachment/callback, resource-cap, executor and clock contract. Keep
Java mutable aliases, arbitrary callbacks, seeded fields and diagnostics as
separate source characterization unless explicitly supported. The native
path will be `native/services/azureus_router_contact`; implementation is drafted
separately under persistent `build/recovery/active-drafts/router-contact-native`. Routing
buckets/timers/bootstrap/eviction/pings/observers, transport-result application,
DB storage and persistence remain outside this record phase.

Acceptance for this first stage is unchanged source/plan/fence proof; actual
compiler/JVM logs and exact raw observations; immutable fixture hashes and a
complete class map; independent source-freeze review; persistent failure/work
copies; and honest task/registry evidence with null port/integration hashes.
Native qualification and implementation commits are later prerequisites for a
verified component. Source compilation or fixture creation alone is insufficient.

The final source tools are `migration/tools/azureus_router_contact_oracle.py`
and `migration/tools/azureus_router_contact_fixtures.py`. Default invocation
inspects source or validates the immutable pre-execution plan without compiling
or executing Java. Only explicit `--build` or `--freeze --reference` actions
start a compiler or source JVM, after root scheduling. The first actual helper contains ten classes, pinned by the frozen source
manifest. It uses the unchanged d77e... harness and has a complete persistent
source/class/log/reference copy. No historical pre-relocation class proof is
invented. Source freeze evidence supersedes the earlier static-only snapshot.

Original source audit, planned commands and pre-relocation review are retained
under `migration/evidence/P06-27-router-contact`. The source audit's 18 files
and 106 context methods do not imply implementation of the surrounding router,
transport or real clock. The exact record is all 26 methods and nine fields;
diagnostic dependencies remain separately classified. Relocation inspection
and the unchanged 280-source P26 qualification guard are recorded in
`relocation-inspection.json`. No registry port/integration hashes or native
commits are assigned at this source-only stage.

Source manifest SHA256: `8b48930b99574e0a83aa9edc914149fdb3ba9cae5e54c394815c5e3f8e7e3eed`.
Actual source helper: `/tmp/biglybtc-router-contact-oracle-hpweusi3`; full persistent copy contains 47 files.

Work-in-progress native/source checkpoint: `691376ed23bf65025800c9385dfaea774ed1462c`;343 qualification inputs plus the separate retry source match actual Git blobs. Full sanitizer gate remains unresolved; source port/integration hashes stay null.
