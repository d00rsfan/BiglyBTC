# Original replication eligibility and query batches

Native `24cec5ff4b3c65fa80815cf185a10d0a3ca7881a`, tree `cf1538fed1230009c0aa2a5c19be2f0ecd680fa0`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Actual original DHTDBImpl.applyRF produced 24 cases under controlled wall and
monotonic observations in an isolated generated-profile JVM. Only that JVM's
SystemTime provider is replaced via reflection/Unsafe and restored afterwards;
no original source or application profile is modified. The real original method
and real DB values execute. Seeded original RNG consumption is verified, with
first-observation deferral, due/future/equal deadlines, min-frequency/oldest-value
phase, zero frequency, mixed default/zero factors, diversified mappings and
signed-long wraparound. Native matches decisions, resulting deadlines and which
clock/RNG observations the original consumes. All 256 replication byte values
and finite input/jitter bounds are covered. These pure decisions grant no DB,
profile or network authority.

The query batcher creates owning request bodies using the already qualified
original encoder for exact sizing. A large final prefix is preserved across
packet boundaries; each packet fits 1400 bytes and at most 384 rows. Owned-body
order and duplicate rows are retained. IPv4/IPv6 headers, 1701 mixed-width rows,
1000 zero-width suffixes, empty liveness groups and invalid limits/widths are
covered. Batch planning neither sends packets nor authorises a peer; each later
physical send must still acquire the ordinary current source permit. This is
not a claim that the original large-query splitter is correct or a reproduced
upstream bug report.

The reference records actual DB creation times after the original constructor
normalises future timestamps. This corrected an initial extreme-clock fixture
projection; the existing native DbValue constructor already matches Java. An
initial missing brace in the new batch helper caused a diagnostic cascade; it
was corrected, and the new test targets now cap GCC errors at three.

Both focused GCC16/C++23/Qt6.12.0 suites passed without skips; see
[qualification.json](qualification.json). No application build was repeated for
these currently inactive preparation helpers, and no daily image or live-profile
acceptance was performed. Next work is actual survey topology/contact state,
owning DB candidates, query aggregation and token-first repair forwarding. The
survey/query components and broader F09 remain in progress.
