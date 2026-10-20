# Saved seven-day input fixtures

These are hand-shaped generated VERSION1 inputs, not an original user profile.
The actual unchanged committed LongTermStatsBase Java reader produced every value
in expected.tsv under Java 17 with inert providers and a nonnull RecordAccepter.
No expected parser or total algorithm was rewritten.

Three profiles each contain seven explicitly enumerated UTC dates: 18 regular day
files are present and 3 leaves are missing. manifest.json lists every exact input
path, presence, size and SHA-256. expected.tsv has 15 TAB-separated fields.
day_index 0..6 is oldest to newest; hour=-1 selects one complete member day and
0..23 selects one hour. The 525 independent queries yielded 3150 scalar values.
Each period ends inclusively at the next boundary minus 1 millisecond.

The six signed counter order is protocol upload, payload upload, protocol
download, payload download, DHT upload and DHT download. Baselines are not traffic.
Missing, empty and header-only periods return zero arrays in this Java API;
native presence and observation metadata must retain their distinctions.
Real zero and signed adjustments are included. The cases cross leap/month/year
boundaries and cover the earliest complete native interval, inactive zero and
negative anchors, overlapping sessions, CRLF/complete EOF and ignored end times.
Rollover spills in a prior file are excluded by each independent selected-day
query. Do not sum these rows or describe them as equality to an original
whole-week Date-range query or rolling graph. No weekly total is supplied.

Original source: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, reader SHA-256
1f9e0967309777ad4c19be9ee123351c9a21755337a0d48fa52dd3752de5ca7f.
The cache, writer, ordinary session owner and original UI were not activated.
All dates are before the fixed 2030 provider clock. Native malformed/version/
overflow/limit policy, missing-state shape, cancellation and filesystem safety
receive zero Java equality credit. The older P04-18 corpus and its 2550 scalar
comparisons stay unchanged.
