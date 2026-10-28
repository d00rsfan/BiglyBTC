# Qualified bounded one-key database mapping

The C++23 owned mapping is qualified against original Java commit
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Three fresh GCC16 Release,
Clang24 Release and Clang24 ASan+UBSan+LSan Debug lanes each pass ten CTests:
2,745 original semantic comparisons,318 native ownership/bounds assertions and
1,149 CLI policy observations across25cases. Each also retains6,767 DB-value,
1,091 DB bounds,943 DB CLI,3,644 wire-value and87,230 node comparisons.
The2,881 callback/iterator/alias/getter/diagnostic/key/short-key source-only
observations retain zero native equality credit, including35 source exceptions.
The source corpus remains5,626 unchanged observations. No JVM ran in this matrix.

The explicitly approved one-line shared stdio adapter is used without changing
core behavior, expected output, response flushing or any timeout. Original280 P26
and221 pre-native input maps remain verbatim with only the documented driver and
task metadata overrides. Earlier records remain historical, not silently rebased.

Independent evidence validation re-parses all thirty actual CTest/LastTest
sections and checks499 source inputs,493 actual runtime/dependency files,
eight helper roots/113classes,5,347 original core classes,70historical evidence
files,37 historical artifacts and three historical caches. All39 commands and
their quiescent captures, two preparation snapshots,36 binaries/archives,
compiler dependencies, strict C++23 flags and semantic caches were checked.
A live manager observation records TracerPid0 for the exact owned launcher.
CTest leaders have untraced launch-path attestation; no direct per-CTest
TracerPid measurement is claimed.

The first independent postvalidator failed because its JSON-line selector
assumed status was the first key. The actual Mapping policy object has sorted
keys. A separately reviewed one-line parser repair accepts JSON object key order;
all count/hash/status assertions and original qualification bytes are unchanged.
Both validator sources, the original failure and the review remain preserved.

Actual implementation checkpoint:
`88ffa2eeb782bf1f775689d4f078af469325a188`.
Separate Git-blob verification matches351 same-path inputs and148 explicit,
byte-identical archived ignored/external inputs. It does not pretend that an
archive alias was the physical compiled source. No future/self-referential
commit is used, and original Java HEAD and user index remain unchanged.

This implements only bounded normal one-key mapping policy and owned intentions.
Actual callbacks, mutable Java aliases, iterator/destroy/getter/diagnostic
contracts, bloom/flood/bans, expiry application, DB facade/persistence, routing,
network and normal client integration remain separate work. Wider source
first-port/integration hashes remain null. This does not complete a feature,
plugin, application, release or migration phase, nor prove client performance.
