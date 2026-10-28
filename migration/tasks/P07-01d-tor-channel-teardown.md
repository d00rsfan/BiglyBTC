# P07-01d Quiet standalone Tor control-channel destruction

Status: implemented lifecycle correction; all three fresh toolchains pass.
Evidence: migration/evidence/P07-01d-tor-channel-teardown/{matrix.json,review.md}. Native lifecycle correction within the existing bounded
control-channel component; original aznettor baseline
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a` remains unchanged.
The earlier ownership-wrapper ASan trace proved that socket destruction may emit
disconnected while QObject connections/QPointers and destroyed member references
are still live. The wrapper guards its own composition; standalone channel
lifetime needs a quiet destructor as well.

Stop the deadline, mark Closed, block socket signals and abort while members
still exist, then cleanse secrets. Preserve explicit stop, EOF/failure and wire
behavior. Preserve all24 original channel cases byte for byte; add four rows
covering dual deletion callbacks and all public signals during Ready/pending
teardown, including exact peer closure/command counts. Retain ownership44,
source95 and SAFECOOKIE1024 regressions: five CTest rows/167 Qt passes expected.

Only newly owned in-process numeric loopback peers run. No Tor process, existing
controller, cookie/PID file, proxy, original Java program or live profile is
accessed. No shutdown/HALT/process adoption is implemented. Qualify three fresh
persistent GCC16/Clang24/ASan+UBSan+LSan builds; retain source/runtime/helper/binary
and original HEAD/index guards. No release/full-client baseline advances.

Native implementation: `4067fb0c16df05153f04fc2453071aed418a30ff`; all33 qualified input Git blobs verified.
