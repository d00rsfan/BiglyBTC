These immutable expectations come from the prior actual Java17 run of the full
unchanged OverallStatsImpl at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f with inert
typed providers. `original-java.jsonl` is unfiltered raw output. `inputs.json`
is the already frozen literal input corpus, containing no numeric expected output.
`replay.inc` only translates those input commands and copies numeric returned
values/maps from that raw output. No native accounting algorithm produced an
expectation. Derivation recipe is preserved in the original author capsule.

The replay checks 95 successfully constructed scenarios, 164 observations and
91 captured save maps. Four Java provider/construction shape failures are named
in manifest.json and excluded from the typed reducer domain. Provider call counts,
registration/lifecycle behavior, real filesystem decoding/saves and whole-client
statistics are outside this comparison. A captured Java void-save intention is
not proof of a committed native write. Native compilation/comparison remains
pending until the owner runs and records it.
