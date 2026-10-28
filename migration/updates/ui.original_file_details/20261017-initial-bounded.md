# Original file-details metadata: initial bounded self review

Implementation `781467117ba6a5f119b1ae92eda456468bfa48a8`; original BiglyBT
baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Codex self review only;
this is not an independent review or a full FilesView migration declaration.

Source scope: FilesView presentation and original TOTorrentFileImpl file indices,
paths/padding, DiskManagerUtil saved priorities and GlobalManagerImpl inventory
selection. Existing original-layout and file-policy adapters remain the behavior
source; the view does not invent a second geometry or change priorities.

The background lease owner reads original state and saved policy, then delivers
an owned, bounded Qt snapshot. The Files dock sorts/filters by stable original
file indices, hides virtual padding by default and rejects stale generation/hash
results. Cached display names stay authoritative. Refresh and display work with
payload directories unavailable and retain all original profile/cache/payload
bytes. No file progress is shown without an actual source for that value.

GCC evidence: seven affected unit/model/lifecycle/read-only cases PASS, three
torrent variants and a stopped+pause-data overlay; 20000-row bounded-model case,
same-index updates, stale selection, cancellation/retry and padding toggle.
See P04-02/gcc-files-7-ctest.txt and gcc-original-files-results.json. The v2
screenshot was visually inspected. Existing transfer regressions, Clang and
sanitizer validation are pending and will be recorded as separate actual runs.

Remaining source behavior includes file-priority/skip edits, file progress and
availability, open/reveal/rename/move/delete, tree/table modes and all columns,
links, persistent view settings, menus, keyboard/accessibility qualification,
platform behavior and broader desktop integration. Original maximum path and
metadata bounds still apply. The component remains in_progress, and both
first_ported_commit and integrated_through_commit remain null.
