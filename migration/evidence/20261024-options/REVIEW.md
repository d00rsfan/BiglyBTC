# Tools → Options: qualified native preview

Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; native implementation
`c191e73cb0feffbd4a61f1bef67df7f5dcb8d621`, final test recipe `6dc23d24f1c2008ae817b7da3ee54abe15645572`; libtorrent source `740a0b9aeabe00e762cc0efe4a0f27593db2550b`.

Tools → Options now opens classic Connection, Advanced, Proxy, Transfer, Queue
and Files pages. Fifty original keys/defaults/types are represented. Apply/OK/
Cancel, dirty Save/Discard/Cancel, reload, masked passwords and field validation
use immutable session/request/revision packets and worker-owned IO. Opening the
dialog does not start a provider, probe a route or create a configuration file.

Only changed fields are written. Unknown/raw values and untouched malformed
fields survive; stale snapshots conflict before publication. Held original
profile locking and ConfigFile backup/saving policy remain in use. The fixed
original filename is biglybt.config. Primary and verified all-absent snapshots
can be saved; recovered snapshots remain diagnostic/read-only. Input is bounded
to 4 MiB. A conflicting dirty draft can be discarded by closing/reopening Options;
automatic draft merging is not implemented.

Supported speed limits and native automatic-queue upper caps apply without
recreating the owner. Ports, bind/proxy/discovery/encryption changes stay pending
restart and never reroute current work. Restart status is compared against the
active policy, including after further saves and reloads. The saved default
directory prefills future torrent/magnet intake without moving payloads.
Queue caps cover native auto-managed upper bounds with 0 meaning unlimited;
original seed ranking, minimum downloads and all scheduling are separate work.
Original-only settings are labelled, including new-import incomplete naming,
automatic speed/LAN policies and minimum downloads. Unsupported combinations
reject validation before saving; untouched sparse defaults are not materialized.

Actual retained original COConfigurationManager loader/getters/saver passes
Java → native → Java → native configuration round trips, including integer,
boolean, proxy-string, path and opaque Unicode/list preservation. No original
core rebuild or core/network startup. Initial r1/r2 startup timeouts remain
recorded; r3 uses the already-documented launcher-thread Debug warmup with the
same ReferenceFence and exact retained classes. Current timeout causality was
not established by a usable JVM dump; the existing warmup resolved execution.

The standalone actual Host probe verifies live17/19 then23/29KiB/s readback,
actual proxy remaining disabled, pending restart retention, CAS rejection leaving
actual limits unchanged and joined close/lease release. Saved caps/binds are
distinguished from actual readback; the separate owner tests qualify queue caps
and routing preservation via get_settings and actual owned payload transfers.

Primary GCC initially passed 17/18; immediate QWidget close approval was repaired
and only 4 affected GUI suites repeated, all passing. Final Clang 18 and leak-enabled
ASan/UBSan 18 pass at the identical implementation inputs. The one existing private-ABI vptr exception remains explicit;
all other checks stay enabled. Hosted exact-source desktop 151/smoke 13
and SDK 2 pass. Initial optional-target/preflight failures are retained without
execution credit; 12 cached static test links were refreshed without source changes.

Private Qt6.12.0 with approved uninstrumented LT passes 13 smoke rows and 18 actual
related suites at the implementation source. A CMake-only runtime-test gate repair
then passes 4 affected suites plus 13 smoke at the final source. All selected test
and product binaries are proven byte-identical; passing broader execution is
reused. No production C++ or headers changed with the test-registration repair. Two Options images and 28 classic/Statistics captures are retained.
The 114-file portable is promoted to ~/Java/BiglyBTC/dist; manifest
`d05780d79469a113aef41efb50861cd02d2948341a0a4d10673a2923a2394725`. Full previous dist: `/home/yurii/Java/BiglyBTC/build/options-20261024/portable/options-final-r2/previous-dist`.
No running original Java/profile/payload or system installation was accessed.

This completes the declared settings preview chapter, not every original Options
page or full compatibility. CF-03 retains additional routes/policies and CF-04
retains full scheduling. CF-01–CF-08 and their concrete remaining checks stay in
the plan. No public-swarm, compositor/scaling or measured performance claim.
