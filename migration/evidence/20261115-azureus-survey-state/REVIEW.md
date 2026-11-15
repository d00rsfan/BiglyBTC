# Original contact state and mapping identities

Native `e0e216d6b40832923a751fb2d220cc63cce47ca6`, tree `180d8ac47419d5acb20d301f797a4efcc21612d6`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The actual original SurveyContactState executes 44 controlled transitions in an
isolated generated-profile JVM. Native matches duplicate and empty-mapping
membership, two different objects with the same key, first/second/subsequent
failures, success, same/changed instance, explicit use-time changes, strict
inactivity and lifetime boundaries, rollback and signed creation overflow. The
original getMappings returns its set directly: a live empty mapping must retain
membership. Native retains bare DB-scoped IDs and bounds each state to 8192 known
identities; this preparation does not yet implement the normal owner/LRU.

The DB now allocates monotonic, nonzero per-record incarnation IDs independently
of content/store receipts. Updates and empty-but-live records retain identity;
deletion/recreation receives a new one. Identity observation performs no clock,
ordinary read or persistence effect. IDs belong only to the current DB session,
are not saved to BiglyBT configuration and are not source/STORE authority.
Generated DB checks cover updates, removal, recreation and expiry/refill before
record deletion, plus stopped/closed observation and original clock effects.

Focused GCC16/C++23/Qt6.12.0 checks and DB/cache/query regressions passed without
skips; see qualification.json. A private arithmetic-helper/member name collision
was caught and corrected. No full application link was repeated at this inactive
preparation stage, and no new daily or live-profile acceptance was performed.
Continue bounded DB candidates, normal state/LRU and source-gated runtime;
F09 remains in progress. Removed incarnations may later be discarded from native
state to bound storage, but a still-live empty record must not be pruned.
