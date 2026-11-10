# Shared SDK actor and asynchronous chat profile

Implementation `e4071bb8d40eb57a6a38cd4c0095c8325c7a0dd0`, native tree `f84d5a360f9d70e26d217c8f661891b0cbfbd49e`.
Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, original azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`, SDK upstream
`740a0b9aeabe00e762cc0efe4a0f27593db2550b` plus the prior qualified provider
and [shared UDP overlay](../20261110-chat-shared-udp/REVIEW.md).

The existing Azureus actor now accepts an explicitly owned shared physical
channel. Its normal parser, source/peer/network/transaction checks, deadlines,
DATA reassembly and callbacks are reused. Ingress is capped at64 datagrams;
receiving a frame never grants peer admission. The socket-based path remains.
The SDK adapter uses a128-frame SPSC mailbox and Linux eventfd. SDK callbacks
copy fixed-size frames without Qt calls, allocation or locks. At most32 frames
are delivered per Qt turn. Delivery/closure belong to that Qt owner. A still
attached observer is revoked before detach; copied channels cannot revive it.

The exact tested SDK image and all47 symbol bindings are checked before attach;
preload/audit declarations and interposed symbol mappings refuse admission. The
normal app runtime cohort is not changed or promoted. The optional test target
uses the explicit new candidate; the reused idle cache has normal app runtime
roles disabled pending a new app cohort. The protected user candidate is unchanged.

Chat profile IO reuses the existing eight-slot broker. Application results become
visible after the worker releases its source gate. Cancellation discards a pending
identity, submission exceptions stop the broker, and external lifetime joins
remain outside the source gate. Constructing a broker result once fixes GCC16's
expected-union false positive without suppressing the diagnostic.

Final new SDK/Qt tests pass (IPv4/IPv6 fragmented6000-byte DATA, ping, exact listener,
source refusal, ingress bounds, close/re-attach, Qt callback thread and declared
preload refusal). Final async-profile tests pass (stable generated identity,
source-gate release, timer responsiveness under contention, cancellation/stop).
Existing DATA and broker suites passed in the earlier aggregate. Its new-profile
fixture failed because QTRY evaluated a destructive pull twice; the fixture now
retains the first result and passes in the targeted rerun. Earlier missing-Boost
and GCC16 compiler failures remain recorded locally. No new SDK build, sanitizer,
public routing, real profile or operational room result is claimed.

This is an infrastructure milestone. Room scheduling, Public discovery, relevant
network/key policy, application/menu binding, nickname/managed-room compatibility
and genuine original-room interoperability remain. Continue the authorised chat
chapter; no stop was requested after this milestone.
