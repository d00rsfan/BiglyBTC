# Original pause ownership codec: bounded self review

Implementation `1c81314a6530f6ff65761e73214525f6ba1dac21`. Plugin baseline
`1ee6a53dae017696d2f0002e4a0d74f5a9476af5`; original ByteFormatter baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Codex self review only.

The native codec indexes canonical ownership identities while retaining the
complete original input. It matches Java grouped uppercase formatting, ASCII
trim, exact-case lookup, mixed CR/LF/CRLF parsing and first-only duplicate removal.
Edits return bytes; no physical writer, engine application or native host is enabled.

Unchanged original private helpers were called on ten inputs with a constructed
thread that was never started/run and no plugin source overlay. Nine inputs
match membership across contains/add/remove. The foreign unterminated final
line demonstrates Java merging and losing new ownership; native append explicitly
rejects it. Removal retains unknown raw bytes instead of Java's destructive
normalization/reencoding. These differences remain visible, not hidden as parity.

All five standalone suites pass on GCC16, Clang24 and Clang24 with address,
undefined-behavior and leak checks enabled: policy, frozen XML/time,6336 original
thread tick states, ownership bounds/50000 indexed lookups, and ten frozen helper
observations. Exact source/binary capsules and original hashes are in P04-06.

Required remaining work: durable original ownership-file persistence, native
pause/resume acknowledgement, batching/concurrency/lifecycle, tags/AutoSpeed and
editor integration; Windows operational checks; unsupported truncated-line
policy and release provenance. Component stays in_progress; first_ported_commit
and integrated_through_commit remain null. No whole plugin/phase is verified.
