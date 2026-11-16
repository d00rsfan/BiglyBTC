# Download-state tag event policy

Native source `e3f5caeed6a30f6738ed91a11fa7424c693e9d3a`, tree `053d77590e4c6409a61fa7c34e3c858c704b9060`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The core now owns the original fifteen externalised tag IDs and state transition,
activity, operation and removal value policy. Transient initialization/checking/
ready states preserve the preceding main tag; finishing may leave no main tag.
Queued completion, stopped/paused synchronization, complete/incomplete cached
membership and derived cleanup follow the original methods. Checking and Moving
operation callbacks deliberately use the original boolean rather than counting
overlapping operations. Removal leaves the original user-data caches intact.

An isolated generated helper invokes the actual original DownloadStateTagger
stateChanged/updateActive/removeInitiated/remove methods and the actual anonymous
operation callback body. Recording tag boundaries replace storage/listener effects;
no full Java client/tagger service or live profile runs. The 259 scenarios contain
485 observed steps: all sixteen supplied state values, completion and pause
combinations, transitions from six primary states, transient history, rates,
total deltas, first observation, signed long overflow, destruction, null-download
operations and overlapping operations. Native replays every membership, primary/
completion/activity cache and active-stamp decision. Rates are signed Java long,
not int; the initial test proxy returning Integer failed and is excluded. A native
test string-view compile error is likewise excluded from the final acceptance.

GCC16 Release/C++23/Qt6.12.0 focused build and replay pass. The policy contains no
Qt, IO, timers or effect authority. It does not yet supply a complete running state
source: lifecycle wiring, original ten-second activity cadence, source provenance,
localized names, partial-state membership and composition with other tag types
continue in the same chapter. No application relink is needed for this isolated
core policy; it will be linked once at coherent consumer integration. No live
profile, payload, running client, protected SDK, current daily or launcher changed.
No registry/F13/chapter completion or full integration baseline is claimed.
