# Original read-only file details in the native desktop

State in_progress; owner Codex; self review only. Baseline BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, TOTorrent files, original inventory
priorities, FilesView/FilesViewMenuUtil and file-table columns. Implement a bounded
owned snapshot of original file indices, relative metadata paths, sizes, signed
priorities, skipped flags and virtual-padding status. Preserve original indices
across proxy sorting/filtering; hide virtual padding by default with a view toggle.

Read metadata and saved policy on the existing background lease owner, only on
demand. Do not read payloads, hydrate/mutate caches, rewrite configuration or show
invented file completion/progress values. Retain authoritative cached library
display names. Reject stale detail results after selection/revision changes.

Attach a Qt model/view file dock and shared Files action. Test real generated
v1/v2/hybrid and priority profiles, metadata byte preservation, original indices,
Unicode/empty/pad rows, selection, stale requests, repeated teardown and bounded
model sizes. Per-file editing, file open/move/delete operations, persisted view
preferences and complete file-view parity remain distinct required work.

GCC16 Release build passes. Seven affected cases pass: readiness regression,
existing library model/lifecycle, new file model/selection/stale handling, three
original torrent kinds with offline payloads, and an original stopped+pause-data
profile overlay. All profile/cache/payload digests remain unchanged. Cached
names deliberately differ from stale raw state names. Evidence is in
`migration/evidence/P04-02`; the v2 offscreen screenshot was visually inspected.
Original complete/cancel/close/multifile transfer UI regressions are running;
Clang24 and ASan/UBSan/LSan affected UI checks remain pending. This does not
qualify complete original file-view parity or any live-user migration.

All nine GCC original transfer GUI regressions PASS after this implementation:
v1/v2/hybrid completion, cancel, deferred close, all three multifile variants
and original priority/rate-limit preservation. Clang also passes all seven
focused file-view cases. Its four selected transfer/close/cancel/policy cases
are running; new ASan build/tests remain pending. Both GCC batches and Clang7
have distinct source/binary capsules pinned to
`781467117ba6a5f119b1ae92eda456468bfa48a8`; no new full43-suite claim is made.

Updated ASan/UBSan/LSan build and all11 affected cases PASS with leak detection
enabled, including four read-only original profiles and pure-v2/cancel/close/
original-policy transfers. All7+9 GCC and7+4 Clang batches also pass. Exact
implementation remains781467117ba6a5f119b1ae92eda456468bfa48a8; newer ownership
work does not alter the compiled desktop. Broader file-view/platform parity and
editing remain required. No full43-case rerun or release qualification claimed.
