# Saved history oracle fixtures

These hand-shaped VERSION1 record files are derived from committed original
LongTermStatsBase writer/parser source at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
The complete original LongTermStatsBase class was compiled unchanged with Java17
(javac --release8), together with its actual LongTermStats/Listener interfaces,
minimal inert dependency stubs and a subclass setting only the six-entry count
and private generated statistics directory. No original Core/profile was started.

expected.tsv is unfiltered actual Java stdout:425 day/hour queries,2550 signed
component totals. The nonnull accepting RecordAccepter bypasses all caches.
Columns/order/date/hour conventions and raw hashes are in manifest.json. CRLF
input bytes are intentionally preserved with Git -text attributes.

Java zero arrays do not establish native observation presence: an absent hour
stays unknown in the native view; an anchored observed zero remains a real0.
Malformed/version/overflow/limit/cancellation policy and observation counts are
native-only tests, not Java equality. Header baselines are not included in totals.
The after-end fixture characterizes reader anchor retention using valid record
forms; it is not claimed to be an ordinary writer lifecycle after session close.
All other writer behavior is source-audited; no live history writer was exercised.
