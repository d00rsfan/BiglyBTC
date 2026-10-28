# Qt Tor control channel

Experimental shared host service using Qt6.12 Core/Network and the qualified
aznettor SAFECOOKIE proof library. One connection per instance, owned by one Qt
thread. The caller supplies a newly generated cryptographic client nonce, cookie
bytes and an explicit numeric127.0.0.1 port. It performs no DNS lookup and sets
NoProxy explicitly. No Tor executable, profile or cookie file is opened here.

The asynchronous handshake has one deadline (default30 seconds), verifies the
server before sending controller proof and enables commands only after the
original250 OK acknowledgment. Errors/cancellation close only this socket, clear
cookie/nonce scratch data and stop the deadline. No process is signaled. There is
no periodic idle polling. Failure diagnostics contain fixed text, never proofs or
raw transcripts. The caller owns any post-authentication reply data and command
lifetimes, which may include sensitive onion-service information.

Bounds:4096-byte socket read buffer,1024-byte decoded lines/commands,64KiB queued
output and32 reply callbacks per event-loop turn. Queued cancellation can run
between bursts. Partial lines, fragmentation, EOF, invalid/tampered proofs,
timeouts, failed authentication, callback cancellation and immediate QObject
destruction are covered using newly created in-process loopback peers only.

This service is not activated in the application and does not launch or take
ownership of Tor. Production nonce generation, original cookie file protection,
process ownership/idle-stop/restart, complete transaction/event demultiplexing,
onion identity lifecycle, settings/UI and SOCKS/privacy integration remain open.
The source behavior baseline is aznettor
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a`, ControlConnection framing/connect.
Only the stated authentication/channel behavior is covered, not the complete
original connect/ownership/getInfo/keepalive workflow. GPL-2.0-or-later.

## Post-authentication ownership setup draft

The separate `OwnershipHandshake` wrapper preserves the existing channel API:
`ControlChannel::Ready` still means SAFECOOKIE authentication completed. The new
wrapper performs original `TAKEOWNERSHIP`, then
`RESETCONF __OwningControllerProcess`, then `GETINFO version`. Only after the
terminal `250 OK` does the wrapper become Ready and expose its bounded owned
version-info bytes. An unsupported ownership command fails without fallback.
The last nonterminal250 reply projected after its first four bytes is retained,
matching the original `getInfo()` result, including an empty result when only
the terminal acknowledgment arrives. This is not a normalized Tor version.

The caller must already own the newly started child behind the supplied numeric
loopback peer. The wrapper owns only a socket. It never discovers/adopts a
daemon, reads a cookie/profile/PID file, launches or signals a process, sends
SIGNAL HALT/SHUTDOWN, activates a proxy, or changes application behavior. Tests
create a fresh in-process127.0.0.1:0 server and use supplied fake cookie/nonces.

Native limits are explicit: one fixed steady-clock deadline of at most30seconds
across authentication and all setup commands, at most32 GETINFO reply lines and
4096 trimmed reply bytes, plus existing1024-byte framing limits. Deadline expiry
is checked again on every reply; command acknowledgments do not reset it.
Unexpected post-setup lines and EOF fail this bounded connection; general
transaction/event demultiplexing and periodic keepalive are separate work.
Signal callbacks may stop/delete the wrapper at every state, and stop is
idempotent. Diagnostics contain fixed text only.

The absolute deadline also guards each channel authentication transition, so
late connection/challenge input cannot send another authentication command while
timer notification is delayed. Dedicated late-connection/challenge rows suppress
timer signals and require transition checks to enforce the deadline themselves.

The independent93-row original-source fixture executes exact getInfo/acknowledgment
control flow over supplied Java StringReader characters. Its ISO-8859-1 encoding
of strings is an observation format, including the Latin1 row. It does not prove
the original socket InputStreamReader's platform-default UTF-8 decoding of raw
non-ASCII or malformed byte sequences. Actual wire parity here covers the stated
ASCII setup transcripts; charset/decoder equivalence remains excluded.

Source: aznettor `ae8efb8d40fb38246ac1790ab5cfb11584689c4a`, exact
`TorPlugin.ControlConnection.connect` command order/acknowledgment checks and
`getInfo` reply projection. The one-deadline/line-count/aggregate-byte policies
are stricter native bounds. No complete process-lifecycle or plugin baseline is
advanced by this draft. No build/test result is claimed until qualification.
