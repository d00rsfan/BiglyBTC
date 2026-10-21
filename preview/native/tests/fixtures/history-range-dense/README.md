# Dense rolling saved-history numeric corpus

31 synthetic daily history files, 2024-02-01 through 2024-03-02. Each has an
original-format midnight s,1 anchor and 1,440 actual minute-delta input records,
ending at next midnight. The 44,640 records have small varied signed fields in
all six original counter positions. No user/profile data or generated expected
accounting algorithm is included.

The unchanged raw expected.tsv is actual stdout from the complete original
LongTermStatsBase Date-query reader at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
compiled with isolated inert providers on JDK17 (Java8 source/target). The clock
is explicit, the nonnull accepting filter disables caches, and forbidden provider
calls were zero. Runtime/version outputs and full source-blob hashes are retained.
All 31 input DAT physical identities and bytes stayed unchanged in that execution.

Day has 1,440 one-minute bins; Month has 720 one-hour bins. Each also has its own
independent whole Date query: 2,162 calls/12,972 scalar results. Most bins select
actual records. Midnight filename spill and the current partial minute/hour
exercise differences between independently queried whole totals and graph bins.
Whole expected values are never calculated from the bucket results.

ranges.json declares native invocation geometry rather than Java period-planner
equivalence. queries.tsv preserves the actual inclusive Date arguments. Native
unknowns compare numerically with original zero arrays, earning no presence or
observation-count equivalence. No writer/cache/live manager/UI/whole Statistics
parity is claimed. This is a separate second corpus; the sparse first corpus is
preserved byte-for-byte.

Use the same history_range_corpus.py DRIVER FIXTURES replay, which copies these
data to fresh generated profiles and invokes no Java. Scoped * -text preserves
all raw DAT/TSV supporting bytes; provenance.json binds the input SHA-256 map.
