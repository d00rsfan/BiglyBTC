# P06-28 Original one-key database mapping

Status: bounded standalone native mapping implemented and qualified. Three
fresh GCC16, Clang24 and leak-enabled ASan/UBSan lanes each pass all ten tests,
including2,745 original comparisons,318 ownership/bounds assertions and1,149 CLI
policy observations. Actual implementation checkpoint
`88ffa2eeb782bf1f775689d4f078af469325a188` has499 verified input Git blobs:
351 same-path and148 explicit byte-equivalent archive aliases. See the
[qualification review](../evidence/P06-28-db-mapping/adapter-cin-untie-v2/review.md).
The2,881 source-only observations retain zero native equality credit. Actual
callbacks, aliases, iterator/destroy, getter/diagnostic, DB/persistence and client
integration remain pending. Wider first-port/integration hashes stay null.
No whole feature/phase, client performance or release completion is claimed.

Historical source/native preparation, preserved below:

Status: in progress; independently reviewed native code and qualification tools
adopted, not built or compared yet. Baseline chosen before implementation:
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The independently reviewed V2 source helper preserves 40 complete original
Mapping/nested method bodies and exact HashWrapper/interface fragments. The V1
3,831-command plan and review remain unchanged. V2 adds explicit key/count and
short-key contracts, including separately classified mutable aliases and stale
cached hashes. A new covariant bridge delegates to the unchanged P26 value
classes. All eleven parent classes remain byte-identical.

The source helper compiled before native work and froze 5,626 observations:
2,745 semantic; 603 callback-fault; 219 iterator-destroy; 412 alias-policy;
336 getter-contract; 85 diagnostic; 380 key-getters; 846 short-key-contract.
There are 35 expected source exceptions, including partial callback commits and
iterator boundary behavior. No automatic allocation of native equality credit
is made by this source-only freeze. Planned native ownership, entry/cumulative
payload/effect bounds and callback interfaces require a separate contract.

Observe original direct/forwarded precedence, version rejection and access
ordering, retained equal-version reset, creation-time tolerance/wrap, unique
nonempty reads, insertion/removal/accounting and ordered adapter/bloom
intentions. Original forwarding keys use originator ID only. Real bloom/flood
filters, bans, DB facade/expiry/index/persistence and actual storage callbacks
remain separate work. Preserve source failures and aliases even where an
explicit native ownership policy differs.

Current source work and full verification are persistent under
`build/recovery/active-drafts/db-mapping-audit/oracle/v2/`.
The helper is `/tmp/biglybtc-db-mapping-oracle-fp2ts6mh`, with all 65 generated
files copied under `build/recovery/generated-references/`.
Source manifest SHA256:
`4a0661490209034f05051387e7ede65e7ec27b46a131d18af75e60115a17c87c`.
No ordinary manager/router/core initialization, actual profile, socket, DNS or
running Java program is involved. Source/class/golden provenance must remain
immutable during the native implementation and qualification.

The accepted owned API and eight-file native draft passed independent review.
The relocated source builder, immutable fixture verifier and ten-test runner
also passed independent review. Sixteen files are adopted; seven native code/
test files remain byte-identical to the reviewed draft, with only README wording
updated to distinguish historical draft status from current task evidence.
See adopted-native.json and development/native-adoption under this task's
evidence directory. Use env PATH=/usr/bin:/bin for root-controlled commands.

Next compile the fresh relocated helper: all twenty Mapping classes must match
the original, with eleven verified original P26 parent classes copied into the
same owned root (not claimed freshly recompiled). Never refreeze the5,626 corpus.
Then qualify2,745 eligible comparisons plus native bounds/1,149 CLI observations
and all seven scoped P26/value/node tests on three fresh compiler/sanitizer lanes.
The2,881 excluded rows retain zero equality credit. A future accepted DB input
adapter requires an explicit reviewed old-input override, not a silent rebase.
