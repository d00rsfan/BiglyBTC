# Real bounded configuration write failures

State in_progress; owner Codex; self review only. Extend the existing profile
configuration component at original BiglyBT baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, FileUtil resilient-file behavior.
No native production-code change: exercise existing writeExclusive/ConfigFile
save with actual Linux RLIMIT_FSIZE failures inside disposable child processes.

Four cases per compiler: zero/16-byte limits with SIGXFSZ ignored (native reports
its real write error) or default (kernel terminates only that owned child).
Disable child core dumps; preserve parent file-size/core limits unchanged.
Require an actual bounded empty/partial .saving file, unchanged committed primary
and backup bytes/inodes, native primary recovery and unchanged original Java
FileUtil recovery. ReferenceFence and isolated JVM directories remain mandatory.

Run with GCC16, Clang24 and ASan/UBSan/LSan profile drivers. Abrupt children cannot
execute sanitizer finalization; normal reported-error/read/reopen paths retain
leak checks. This is EFBIG/partial-write coverage, not ENOSPC/fsync/rename/disk or
payload fault qualification. Those broader required gaps remain open. The user's
running application/profile/payloads and system/parent resource limits are never
changed. Results pending; current GCC run uses a fresh generated test root.

All four cases PASS with each of the GCC16, Clang24 and ASan/UBSan/LSan native
profile drivers (12 actual failure cases). GCC/Clang were invoked directly;
the sanitizer batch passed the registered CTest. Every driver hash matches its
retained qualified desktop build capsule. Evidence/parent-limit checks and
original class/harness hashes are under migration/evidence/P02-04. No native
production code was changed. The current CMake definition registers44 desktop
tests; earlier37/43-definition matrix evidence remains explicitly historical.

Coverage boundary: incomplete .saving files are intentionally retained. These
cases prove intact committed-file reads/reopen, not automatic cleanup or a later
successful settings save after the fault. ConfigFile currently rejects an
existing saving pathname on a new exclusive write; owned-error cleanup/recovery
resolution and retry qualification remain required work. Preserve foreign
recovery files rather than treating this scoped test as full fault recovery.

Follow-up regression initially FAILS against the retained old binary: a reported
write error leaves the attempt's own partial saving file and blocks a later save.
The new writeExclusive branch attempts rollback only after its own O_EXCL-created
file has a real write failure, matching the still-open descriptor's device/inode
against the pathname under the held profile lease. Pre-existing/replaced files
are not touched. Directory removal is synchronized best-effort; original primary
and backup are untouched. Abrupt process death and fsync/rename recovery are still
separate. Added checks require two successful later saves and original Java
reopen of the new values, preserving opaque bytes and previous primary as backup.
Implementation is compiling; new behavior is NOT qualified yet. Original twelve
read/reopen-only fault observations remain historical, not rewritten as retry proof.

Final rollback implementation5073f29e81752be5b6cef9ceabddd9e919eaae72: GCC,
Clang and ASan/UBSan/LSan all3 targeted suites PASS. They cover original locks,
foreign recovery/symlinks, configuration/torrent-state crash stages and12 actual
write failures plus6 successful later saves with original Java reopen. Source/
binary capsules and final report maps are retained separately from historical
pre-fix read-only recovery evidence. No full44-test or complete fault recovery
claim is made. The source-engine six-case replay is being repeated on final GCC.

Final six-case own-source-engine replay also PASS on native5073, with five loader
selections verified and all binary hashes unchanged throughout. Retained under
P01-02/final-gcc16-source-runtime.json. No active verification remains for this
bounded change; broader filesystem/recovery/platform gates remain required.

Additional development probe PASS against final GCC5073: a child-only Linux
write interposer performs an actual16-byte write, renames that owned inode away,
creates a foreign replacement at the saving pathname, then injects ENOSPC. The
rollback inode guard preserves the foreign bytes, retained owned partial file,
committed primary/backup bytes/inodes, and native original-value reopen. This is
injected failure/control-path coverage, not a real full disk or general hostile
writer race qualification. Run explicitly with
`python3 native/tests/replaced_saving_fault.py` against the retained qualified
GCC driver/capsule; it is a pinned development probe, not a new CTest matrix row.
Source/interposer/harness/native hashes are retained in replaced-saving-inode.json.
Production code remained5073 throughout; the source-runtime evidence is unchanged.
