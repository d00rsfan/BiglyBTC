# Owned temporary write failure: bounded self review

Implementation `5073f29e81752be5b6cef9ceabddd9e919eaae72`; original BiglyBT
baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Codex self review only.

The initial real EFBIG/SIGXFSZ tests proved committed configuration bytes/inodes
and Java/native reads survive partial writes. A stronger retry check then failed
against the retained prior binary: a reported error left its own incomplete
saving file, and the next exclusive save was blocked. Initial failure retained.

The new write-error branch attempts removal only after this call successfully
created its temporary file with O_EXCL and the current pathname still matches
the open descriptor's device/inode under the held profile lease. An existing
path that prevented creation, or a different inode at cleanup, is left alone.
Directory deletion is synchronized best-effort; the original error is reported.
Primary/backup publication and successful writes are unchanged. Full fsync or
rename error resolution and abrupt-death cleanup are deliberately not inferred.

GCC16, Clang24 and Clang24 ASan/UBSan/LSan all pass the three targeted suites:
original profile locks/foreign recovery paths/symlinks/crash stages, original
torrent-state/cache/backup interop, and four actual write failures plus two
successful later saves per compiler. Native/Java reopen old values after failure
and new values after retry; opaque bytes and old-primary backup inode survive.
Only disposable children receive limits/SIGXFSZ; parent limits stay unchanged,
core dumps are disabled, normal exits keep leak checks. Abrupt exits cannot run
sanitizer finalization. Final matrix and native/source hashes are in P02-04.

This is ordinary owned short-write rollback, not full filesystem fault or hostile
uncooperative-writer qualification. Unknown recovery files, fsync/rename/ENOSPC,
crash cleanup, platform behavior and retry after unresolved recovery remain open.
No whole component/phase is verified; first-port/integration baselines stay null.
Final six-case own-engine replay PASS on this implementation, with five verified
loader selections and original-profile rollback. Earlier six successful cases
and retained old binaries remain separately historical.

An additional pinned GCC development probe replaces the saving pathname with a
different inode between the real16-byte write and an injected ENOSPC. Foreign
recovery content, owned retained inode and committed bytes/inodes survive; native
reopens the old configuration. No production code changed. This tests the guard's
mismatch path; it is not a general uncooperative-writer race or disk-full proof.
