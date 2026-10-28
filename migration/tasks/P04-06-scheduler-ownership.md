# Original scheduler pause ownership file

State in_progress; owner Codex; self review only. Original plugin baseline
`1ee6a53dae017696d2f0002e4a0d74f5a9476af5`, SpeedSchedulerThread
wePausedTorrent/addPausedDownload/removePausedDownload and original BiglyBT
ByteFormatter.nicePrint at `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Provide a bounded owned codec for existing PausedTorrents.conf bytes. Preserve
case-sensitive grouped uppercase identities, Java ASCII trim, CR/LF/CRLF reads,
first-only duplicate removal and idempotent append/removal. Index ownership for
repeated lookup; keep unknown bytes and all untouched line encodings intact.
Native edit APIs return replacement bytes only; no physical writer is enabled.

The original append helper merges a new identity after an unterminated foreign
line and cannot find the resulting ownership. Native append explicitly rejects
that boundary. Retain the original observation and document this bounded
unsupported case. Native removal preserves unknown raw bytes instead of Java's
unrelated whitespace normalization/reencoding; membership must remain equivalent.

Unit checks cover lossless unknown bytes, mixed newlines, duplicate lifecycle,
byte/line bounds and Linux/Windows newline encoding. A new guarded Java 8 harness
constructs but never starts/runs the original thread, invokes its unchanged
private helpers with a host-value environment, and compares native membership
through append/removal for ten generated input cases. The harness compiles;
actual native/original differential and compiler/sanitizer tests are PENDING.

Durable ownership-file persistence, engine pause/resume acknowledgement, batch
application, host concurrency/lifecycle, Windows operational qualification and
plugin enablement remain required before integration or feature completion.

GCC native build and four preliminary suites PASS, including 50000 indexed
ownership lookups. Unchanged original helper differential PASS on all ten
inputs: nine equivalent membership lifecycles and the explicitly rejected
unterminated-foreign append boundary. Source observations and exact input bytes
are frozen under migration/fixtures/scheduler-ownership, with the original
helper logs/class/harness/native hashes in P04-06/gcc-original-ownership.json.
Original thread was constructed, never started or run. No source overlay.
The complete five-suite GCC/Clang replay is running; ASan compilation is next.

All five standalone plugin suites PASS on GCC16 and Clang24: pure policy, frozen
XML/time, unchanged-thread tick observations, bounded ownership units and ten
frozen private-helper observations. ASan/UBSan/LSan build/tests remain pending.
No full plugin or ownership-persistence completion is declared.

ASan/UBSan/LSan all5 suites PASS with normal leak checks enabled. GCC/Clang/San
source/binary capsules are finalized against implementation
`1c81314a6530f6ff65761e73214525f6ba1dac21`. This closes this development compiler
matrix, while physical persistence, host application and the explicit rejected
input boundary remain required integration work. No phase/feature is verified.
