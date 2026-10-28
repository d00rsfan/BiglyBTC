# Tor post-authentication ownership setup review

Original aznettor baseline ae8efb8d40fb38246ac1790ab5cfb11584689c4a.
The exact post-AUTH connect block, getInfo, sendAndReceive and readLine(boolean)
run unadapted over inert supplied I/O in a separate fenced Java helper. Source
file/part/harness/helper hashes and independent93 observations were frozen before
native comparison. Source accepts81 and rejects12; four accepted source rows
retain explicit stricter native line/count/byte/NUL framing policies. Latin1 is
the String observation format, including its non-ASCII row; the original socket
InputStreamReader default-charset decoding is outside this character-level proof.
Actual generated socket setup transcripts cover the stated ASCII wire sequence.

OwnershipHandshake preserves ControlChannel::Ready as authenticated only, then
requires TAKEOWNERSHIP, RESETCONF __OwningControllerProcess and GETINFO version.
Source ACK prefix acceptance, exact terminal250 OK, last nonterminal substring4
projection and empty result are preserved. Unknown commands fail without fallback.
One fixed steady deadline spans authentication and setup; every relevant socket
transition/reply checks expiry. Native caps remain32 version lines,4096 trimmed
bytes plus existing1024 framing. There is no deadline refresh or event/transaction
multiplexer. Post-ready unexpected input/EOF fails this bounded connection.

The wrapper owns only its socket and requires a supplied newly owned numeric
loopback peer. Tests create fresh in-process127.0.0.1:0 servers with generated
cookie/nonces. No real Tor process is launched, adopted, probed or signaled; no
existing controller port, live profile, cookie/PID file, proxy or app/plugin is
accessed. Future QProcess ownership must begin when the exact new child starts,
including pre-auth failures. Original initialize's existing-daemon HALT behavior
is deliberately excluded under the user's running-client constraint. Periodic
keepalive, settings/hidden services, process/proxy lifecycle and plugin activation
remain separate work, with no whole-plugin or full-client baseline advancement.

Native implementation and lifecycle tests came from a user-authorized worker;
root independently froze the source observations and source-driven Qt rows.
Another worker reviewed native callback/deadline/lifetime rules and found a late
authentication transition that could otherwise send after the outer deadline.
Two regression rows suppress timer signals and require transition checks.
The native author independently reviewed the source collector/expected rows.
There is no separate full-plugin end-to-end parity review.

The first GCC matrix found a default-destructor use-after-free when a Ready
callback immediately deleted the wrapper. Child socket destruction could emit
failure while wrapper members were already destroyed and QObject/QPointer were
still live, reentering deletion. A focused ASan run reproduced it; exact failed
sources/logs/binary hash and raw/offline GNU addr2line trace are preserved. The
custom destructor now stops its timer, marks Closed, disconnects and blocks child
signals, and stops the channel while members remain valid. All original42 rows
are unchanged; two external-spy rows require no public state/ready/failure/closed
signals during Ready/pending destruction. Existing channel/auth code is unchanged.

Before the first build, an input guard also identified a historical test-fixture
location helper update already qualified in commit
4ea971c87cdaf5f2c792e220c4dd66643af41a14. The matrix now pins those exact helper
bytes and previous export-follow-up evidence. Original fixture data and C++ code
were unchanged. The failed guard and pre-fix matrix are retained.

Three fresh persistent v2 roots on GCC16 Release, Clang24 Release and Clang24
ASan+UBSan+LSan Debug pass all five CTest rows and163 Qt checks per compiler:
unchanged channel24, ownership44 and source-driven95 (93 cases plus lifecycle).
Original1024 SAFECOOKIE transcript and native proof/bounds regressions also pass.
No warning, optimization or sanitizer-check suppression is used. The configured
LLVM symbolizer path was absent in the initial diagnostic; GNU addr2line resolved
its raw offsets offline. Runtime help verified allow_addr2line for subsequent
qualification diagnostics; leak and undefined-behavior checks remain enabled.
Commands, input/helper/runtime/binary hashes, full logs and persistent incremental
matrix metadata are retained. Original Java HEAD/index/running client and live
profile remain untouched. Continue actual STORE authorization and owned Tor child
lifecycle, then routing/storage and dependent host/plugin integration.
