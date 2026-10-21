# Rolling saved-history numeric corpus

Twelve ASCII synthetic original-format history files, plus a marker preserving an
empty generated profile. No user/profile data. `expected.tsv` is the unchanged raw
stdout of the original BiglyBT Date-query reader from revision
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, executed with inert providers on JDK 17
(Java 8 source/target). Full original source blob IDs and SHA-256 values are in
`provenance.json`; the extraction/provider source capsule is separately preserved
in milestone evidence. The runtime marker/version files are actual raw outputs.

Thirteen ranges produce 5,114 independent Date calls / 30,684 six-counter values.
The month includes 720 hourly bins; whole-range calls independently preserve
filename spill effects. The clock is explicit, the nonnull accepting filter
disables caches, and forbidden provider calls were zero. Normal files cover all
six counters, signed changes/extremes, empty/missing files and recorded zero.

`ranges.json` declares native invocation geometry, not an original period-planner
oracle. Exact-week-boundary All cases use the documented native minimum-one-bin
policy. `queries.tsv` records the actual original inclusive Date arguments.
Native unknown values compare numerically as original zero arrays, earning no
presence, observation-count, planner, writer/cache, UI or full Statistics parity.

Run `history_range_corpus.py DRIVER FIXTURES` through the registered native test.
The replay uses fresh temporary copies and never invokes Java. All raw DAT/TSV
and supporting inputs are protected by scoped `* -text` and SHA-256 maps.
