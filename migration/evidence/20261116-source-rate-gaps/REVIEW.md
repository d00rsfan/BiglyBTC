# Transfer-rate windows after delayed observations

Native source `fd2729d201d1478e57fe4b3c85ef5002c63911f5`, tree `dbc7a7dc0448e0aa9e345f59f9ca131df15d9ea2`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The deferred footer-rate report had a reproducible Native adapter defect.
Controlled cumulative samples representing constant800B/s received payload,
200B/s received protocol,400B/s sent payload and100B/s sent protocol were delivered
normally, followed by a20s observation gap. The pre-fix regression failed because
its displayed receive-payload rate exceeded the known800B/s. No network packet
or real client trace was used: the regression runs with explicit times in0ms.

The source-style adapter had assigned the whole delayed byte difference to its
delivery bucket. It now starts fresh display windows when new bytes span more
than the entire10s window, measured against either actual sample progression or
time since the last fresh accepted owner observation. That fresh baseline has
unknown window rates. Subsequent fresh observations warm the original source
averages normally. No intermediate arrival times are reconstructed. Engine raw
interval rates, traffic totals, lifetime/profile accounting and saved display
preferences are unchanged; no configured-capacity clamp or interval-mode fallback
was added. This adapter qualification does not claim Java event-arrival timing
that the cumulative snapshots do not contain.

An exact repeated sample retains its earlier rate only within the observation
window. Repeated/stale/missing publications cannot keep an old rate alive forever
or advance the last-fresh timestamp. A new genuine idle sample can age existing
windows to zero without inventing bytes or unnecessarily rebasing them. Invalid
counter/time/sequence observations leave accepted state unchanged. Exact10s and
one-nanosecond-over boundaries, distinct owner/sample clock origins, constant
traffic recovery, missing observations, stale duplicates and counter regression
are covered. The input/cumulative accounting remains independent of display
window rebasing. The first new-bucket expectation was corrected to honour the
original exclusion of the current incomplete bucket; that failed expectation
is excluded from accepted evidence.

The unchanged original Average/smoothing primitive passes22 explicit-time groups
and121 assertions. The existing actual-original fixtures are replayed with13
Average cases,9 global cases and316 scalar comparisons; no new JVM run or whole
GlobalManager runtime equivalence is claimed. The projection passes25 groups and
1052 assertions. Fourteen Qt activity-mode tests cover missing values, no interval
fallback, zero, checked component sums, history gaps, queued/hidden views and
renderer lifetime. One coherent GCC16/C++23/Qt6.12.0 application link and the seven
persistent-accounting service groups pass. Unrelated green network/controls
suites from the preceding chapter were not repeated.

This closes the reproduced delayed-observation path, not every possible cause
of the original visual report. Later real-library acceptance can accompany a
user-requested daily. No running client, real profile, payload, immutable current
daily or protected SDK was changed; no new distribution/rollback image exists.
Continue remaining P08 registry/dependency and library-policy work autonomously.
