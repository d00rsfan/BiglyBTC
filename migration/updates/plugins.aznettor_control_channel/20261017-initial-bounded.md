# Qt Tor control channel review

Self review, 2026-10-17. Original aznettor
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a` ControlConnection numeric loopback,
NoProxy binding, SAFECOOKIE, CRLF commands/trimmed reply framing and EOF/failure
semantics. This covers a bounded asynchronous channel, not the complete original
connect/TAKEOWNERSHIP/RESETCONF/getInfo/keepalive lifecycle.

Qt6.12 Core/Network owner-thread service owns exactly one127.0.0.1 socket per
instance and no process. Commands are unavailable until verified server proof
and successful authentication acknowledgment. The single deadline stops at ready
or every terminal state; no idle polling exists. Cookie/nonce scratch data is
cleansed. Fixed error strings exclude all proofs/transcripts. Bounds4096 input,
1024 decoded lines/commands,64KiB output and32 callbacks per event-loop turn.
Queued cancellation can run during reply floods. Signals can cancel/destroy the
object immediately without subsequent member access; QPointer guards cover state,
authenticated, reply and write/bind error callback boundaries.

GCC16, Clang24 and Clang24 ASan/UBSan/LSan pass owned Qt loopback tests and both
original SAFECOOKIE regressions. Each test creates its own ephemeral numeric
loopback server; no existing Tor endpoint/process/profile is accessed. Whole and
fragmented replies, tampered proof, bad acknowledgment, EOF, line/injection bounds,
timeout, invalid input/no connection, immediate callback cancellation/destruction,
authenticated/reply destruction, ready EOF, stopped deadline and flood fairness
pass. Full Qt logs and source/binary/Qt/OpenSSL hashes are in the matrix capsule.
Leak detection stays enabled. No application-wide matrix/performance claim.

The service is not linked/enabled in the current application. Cryptographic nonce
creation and original cookie/onion files, process ownership/start/idle-stop,
command transaction/event demultiplexing, settings/UI and SOCKS/privacy policy
remain required. Source ownership/ABI/package closure and real Tor selection are
unqualified. Modern async/error/buffer ownership replaces the original blocking
control I/O only within the stated slice; this does not authorize direct traffic
fallback or operating another Tor process. GPL2-or-later derived service.
