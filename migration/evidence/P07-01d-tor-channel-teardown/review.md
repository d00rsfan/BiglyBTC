# Standalone Tor control-channel quiet teardown

This corrects a native lifetime defect within the bounded channel component.
Original aznettor baseline remains
ae8efb8d40fb38246ac1790ab5cfb11584689c4a. The existing P07-01c ownership-wrapper
ASan trace proved that default socket destruction could emit disconnected while
channel/wrapper members had already been destroyed and QObject connections and
QPointer still remained live. The prior wrapper fix guarded its composition;
the underlying standalone channel needed the same lifetime discipline.

The custom destructor stops its deadline, marks Closed, blocks socket signals
and aborts while timer/state/secrets still exist, then cleanses secrets. No
public state/authenticated/reply/failed/closed signal can escape destruction.
Explicit stop and live EOF/failure behavior remain unchanged. No Tor shutdown,
HALT, process adoption or periodic/event protocol behavior is introduced.

The matrix reconstructs the entire previous channel source outside the new
destructor and removes only the appended test block/include to prove every
original24 Qt case is byte-identical. Four new rows exercise deletion from both
success/failure handlers and external signal spies during Ready/pending teardown;
they also require exact command counts and peer-observed socket closure.
Native author, independent worker reviewer and root reviewed the correction.
The retained P07-01c failure/ASan trace remains the diagnostic source; no fresh
pre-fix standalone failure run is claimed.

Three fresh persistent GCC16 Release, Clang24 Release and Clang24
ASan+UBSan+LSan Debug roots pass all five named CTest rows and167 Qt checks per
compiler:28 channel,44 unchanged ownership and95 source-driven ownership checks.
Original1024 SAFECOOKIE observations and native authentication bounds pass.
Frozen93 original String-level setup observations remain unchanged; there is
no new Java compiler/runtime invocation or broader socket charset claim.
All33 inputs, dependency/helper/core hashes, compiler binaries and original Java
HEAD/index are guarded, with command/progress metadata saved before execution.
No warning or sanitizer checks are suppressed. Generated runtime namespaces are
copied persistently after each command; native build roots remain on disk.

Only new in-process numeric-loopback peers and generated cookies/nonces run.
No real Tor process/controller, cookie/PID/profile file, proxy, running Java
program or live profile is accessed. The change is not plugin/application
activation, process supervision or full-client/release parity. Continue DB
value/mapping, real owned Tor child lifecycle and dependent host integration.
