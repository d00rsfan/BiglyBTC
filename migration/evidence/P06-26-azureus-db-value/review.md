# Original database-value lifecycle

The entire original DHTDBValueImpl class body and both value interfaces are
unadapted at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The scripted clock,
inert contacts and logging-off seam avoid real DB, SystemTime, routing, network
or profile initialization. Ten source files and22 source symbols are pinned.
The independently frozen7344 observations preceded native implementation:
6767 semantic,76 alias-policy,249 getter-contract and252 diagnostic rows.
Only6767 semantic observations receive native equality credit. Mutable Java
array/contact aliases, arbitrary custom getters and diagnostic formatting
remain source-only; they are not misrepresented as native equivalence.

The C++23 value copies payload/contact ingress into private immutable backing
and shares owned snapshots across ordinary copy and relay. Nullable payloads
differ from present empty values. Retained handles keep borrowed views alive.
Factories borrow a synchronous wall clock without retaining it. Creation and
reset read once and clamp future creation time. Deletion preserves the original
two-read order, empty payload and originator-as-sender behavior. Flags and
lifetime use unsigned byte narrowing; replication control is signed and DB
frequency uses its unsigned high nibble except default-1. The earlier wire
value codec's distinct signed shift remains unchanged and regressed.

The64KiB per-payload cap, defined moved-from state and self-move behavior are
explicit native policies. The fixture CLI additionally bounds table/labels,
line size and cumulative16MiB ingress until reset; those are driver boundaries,
not invented original-source failures or production cumulative storage limits.
Native author, independent source/matrix reviewers and root reviewed the slice.

All three fresh persistent GCC16 Release, Clang24 Release and Clang24
ASan+UBSan+LSan Debug builds pass seven named CTest entries. Each matches6767
semantic rows and passes native lifetime/clock/CLI boundary tests plus unchanged
3644 wire-value and87230 node observations. The first sandbox sanitizer tests
aborted because LeakSanitizer could not inspect threads under ptrace. Their
failed matrix, logs, runtime copy and binary hashes remain under development/.
The narrowly pinned recovery repeats the identical seven CTest entries outside
the tracing restriction against the exact same ASan binaries, with leak
detection still enabled. The final matrix records both the failed command and
the passing repetition; GCC/Clang passes are retained without needless rebuild.
No native source changes, warning or sanitizer suppressions were needed. The7344 frozen
source observations are unchanged. A relocated reference rebuild preserves all
11 generated Java/value/fence classes byte-for-byte and saves all24 generated
files persistently; qualification itself executes no JVM. Selected30 qualified
wire/node inputs and all246 P25 inputs are guarded without claiming a full
socket-matrix replay. All279 qualification inputs, helper/core/runtime/binary
hashes and original Java HEAD/index are checked. One additional recovery source
is pinned separately. Progress/logs and owned runtime
namespaces are persisted after commands; native builds reside on disk.

This is a pure standalone value type, not DB mapping/index/expiry/republish,
persistence, wire adaptation, authority/router/actor activation or a complete
client. No running original application, live profile, actual torrent payload,
Tor/router/public network or original process is used. Continue mapping/router
state and the persistent torrent-session chapter against isolated fixtures.
