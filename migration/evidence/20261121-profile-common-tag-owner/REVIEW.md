# Common profile controller lifetime and explicit private service preparation

The normal PersistentSession can now receive the same shared ProfileTagRates
root used by explicitly prepared private SDK owners. Its startup mode,creating
worker,actual held profile and controller lifetime are checked before startup.
Normal close,terminal close without checkpoint,poison retirement and destructor
release only that session's admitted SDK cohort. An externally held root remains
usable by surviving sessions until explicit final root close.

Actual two-profile control found that initial injection validation alone did
not protect later startup:profile-A controller accepted profile-B startIdle.
The retained pre-fix binary/test returned1. The final Impl::bind guard runs
before profile identity mutation and SDK creation;the same engine then accepts
actual profile A. Root-close-before-startup,foreign worker/controller,missing
startup mode,duplicate injection,cohort proof revocation,and all three normal
close routes are tested on generated isolated profiles and actual SDKs.

OwnedI2pService has explicit startWithTagRates/addWithTagRates entry points.
They consume the controller synchronously on the original Host I/O worker via
previously qualified OwnedProfile preparation;no pointer/reference is retained
by the service,its private Qt worker or the owned provider. Old public signatures
and default active private-rate refusal remain unchanged. Disabled optional
provider variants return Unsupported without touching the controller.

Actual service tests prepare ManagedStorage with tagged shared classes,start
the real private Qt/SDK/source worker lifecycle and join it through terminal
close,plus reject default/foreign preparation before payload creation. They
use no router and grant no private payload/transport throughput credit. The
initial successful-preparation fixture wrongly required unchanged resume;
ManagedStorage intentionally marks recheck-required. Its failed log and fixture
are retained. Corrected tests require refusal state unchanged and final close
state equal to actual postprepare state. No production fix for that assertion.

This milestone supplies lifecycle and explicit service plumbing. SessionBridge
is unchanged:normal Host activation,private-only Host root ownership,live private
source/stamp refresh,pending metadata/promotion,Mixed transport,common global/LAN
and actual tagged private I2P acceptance remain required and continue next.
No chapter/whole feature/global completion,SDK or daily promotion follows.

Original core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f;I2P helper
33d97eca67a9f332e3d4da7e7492ec54f4bec078;Native parent
583a2c4f148c2ed650974684ec5512691f27463a. SDK source and binary remain
unchanged from the all-role qualification. Upstream SDK
740a0b9aeabe00e762cc0efe4a0f27593db2550b is inherited matched2.0.12
archive attribution,not a fresh Git-tree comparison. Full artifact/preservation
and final qualification hashes accompany this review.

Selected ABI1 cache has no private provider target. Its first configure omitted
the ordinary common-owner target because its CMake guard was too narrow. The
correct guard/link use desktop_host/local_engine;explicit service checks remain
all-role/private-provider gated. Its first compilation then exposed an unused
private Source include in the shared profile fixture. The fixture now includes
only ProfileLease/core library types;the actual classic I2P consumer includes
ProfileSource directly. Fixture bodies/data are unchanged. Initial configure,
compile and controlled interruption logs are retained. Final Source includes
these two test-dependency corrections;both applications and all affected test
binaries are qualified after correction.

The latest user boundary supersedes older whole-plan/no-daily notes:complete the
current tag-rate CHAPTER,then build and verify a NEW daily and STOP. This
lifecycle milestone is not that stop boundary;remaining chapter gates are
recorded explicitly above and in the task/plan prefix.

Final qualification:103 unique runtime groups,240 Qt rows,zero failures/skips. Final GCC16,Clang24 and scoped GCC16 ASan/UBSan changed controller,normal engine,service and fixtures pass. ASan/UBSan final changed TUs use O0/g1;SDK/Qt/remaining archives are Release. Both full application builds and selected ABI1 new lifecycle tests pass. No full SDK sanitizer or TSan claim.
