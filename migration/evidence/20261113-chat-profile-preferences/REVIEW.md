# Original chat display and participant profile preferences

Native 80d2976e4b7e8f577ecfc4c54f48e87361f45d3f, tree 82d97af2454198bcaab7724386bfc0c8e7fe01a6. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact selected source/body hashes are
in [source-scope.json](source-scope.json).

Read and edit the original two global hide flags and the ignored/pinned maps
keyed by the uppercase first sixteen public-key bytes. Global booleans use
Java int narrowing/nonzero; participant MapUtils booleans require long == 1.
Only requested typed public-key settings leave the adapter. Unknown properties
and root entries survive updates; disabled fields and empty property maps are
removed. Saved spammer data is retained but not activated, matching the original
commented-out restoration. The legacy pin remains effective on read; unlike
Java's constructor, a read does not rewrite configuration. The next actual
participant update migrates it without losing the effective pin.

Edits require a primary configuration, the expected effective field value,
the held profile source gate and the existing whole-file compare-and-save.
A new copy-on-write dictionary erasure retains immutable original bytes and
unrelated raw fields. An early legacy-pin regression exposed the replacement
of that immutable source during deletion; it is fixed without bypassing CAS.
Malformed property maps are not silently destroyed by an explicit edit.

The original-chat-preferences test executes real selected original key,
constructor, setter and configuration getter bodies with real core
ByteFormatter/MapUtils/BEncoder/BDecoder and explicit in-memory collaborators.
64 read combinations and 256 property updates match original semantics.
Additional regressions cover stale updates, legacy pin migration, unknown
fields, empty maps, missing-primary recovery refusal, key bounds and COW/source
retention. Together with original-general-chat-profile, both tests pass in
0.56 seconds, zero skips. Only generated temporary profiles were used.

This is the profile primitive milestone. Runtime controls, attention/read-marker
handoff and broad chat parity are not claimed here. No running preview, live
profile, daily-use image or SDK artifact was changed.
