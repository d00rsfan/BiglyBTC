# Original per-file policy and bounded activation

State in_progress; owner Codex; self review only. Baseline:
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, GlobalManagerImpl compressed/legacy
priority loading and saving, DiskManagerUtil loadFilePriorities/storeFilePriorities.

Decode the original compact priority-to-index-range dictionary and legacy list
without changing its encoding. Preserve skipped flags and signed Java priority
values. Bound allocations and total range work; reject ambiguous conflicting
overlaps and unsafe malformed activation inputs. Characterize genuine profiles
saved by Java and compare their decoded policies after Java/native/Java handoffs.

Initially qualify only non-skipped linear files whose distinct priority order can
be represented by the candidate engine. Keep compact/reordered/skipped-file and
arbitrary priority-class behavior pending; never silently download skipped files.
Preserve original saved fields and rate limits. UI projections must distinguish
unknown policy from default policy. No live profile or public network use.

Implemented: bounded compact/legacy decoder, exact Java signed/Long.intValue
semantics and rank-preserving non-skipped activation (at most seven ranks), plus
positive original byte-rate limits. Four original-loader value sets and GCC units
pass. Genuine original v1/v2/hybrid profiles with priorities -3/5 and byte limits
32768/65536 continue through native and reopen with those same original policies.
All five rejection cases preserve every profile/cache byte and source payload,
with no peer connection. Actual Qt v2 policy continuation also passes. Full37
GCC evidence is in P03-03; Clang and ASan full matrices are still running.
