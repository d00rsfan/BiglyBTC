# Shared UDP transport milestone for Public chat

Status: lower-layer implementation and owned-loopback qualification complete.
Operational general chat rooms remain in progress.

The SDK now exposes the existing torrent UDP listeners to the Azureus transport
without competing sockets or reuse-port dispatch. The API reports local and
observed external addresses, identifies the receive binding, selects the exact
send binding and preserves the original SDK IP-filter/proxy restrictions. It
checks source permission immediately before sending. Bounded synchronous posts
use locked cancellation acknowledgement so timed-out queued effects cannot run
later. Calls from the SDK executor are refused.

The SDK hook runs after uTP declines a packet. Azureus candidates take the new
path; ordinary BEP5/UDP-tracker handling remains unchanged. Only the framing
candidate is classified here; complete protocol/identity/transaction validation
remains the Azureus actor's responsibility.

GCC16 Release checks cover actual IPv4 and IPv6 Azureus ingress/egress and BEP5
ping/reply, exact local binding, stale authority, non-Azureus sends, SDK IP-filter
refusal and detach. Each IP family also completes a generated32KiB torrent using
uTP with TCP disabled, receives an actual owned UDP-tracker reply, and delivers
Azureus traffic through the same listener during that test. Final boundary cases
cover a send cancelled while queued on a deliberately blocked SDK executor, a
subsequent valid send, broadcast rejection, invalid time budgets, direct-transport
policy after proxy activation and a copied handle after session destruction.

The first optimistic raw uTP-SYN response assertion failed: an idle SDK may drop
that SYN. It was replaced by actual payload transfer, not removed without a
replacement. Earlier fixture compile failures were corrected; the final result
and digests are recorded separately. Full build/test logs remain local. No
sanitizer, full app, NAT/public network or real-room interoperability result is
claimed by this milestone.

The overlay and all seven test families are committed together. Source preimages
and results bind this delta to the prior qualified patched SDK. The immutable
source lineage remains: original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, original azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`, and libtorrent
`740a0b9aeabe00e762cc0efe4a0f27593db2550b` plus the recorded parent provider.

This candidate is not activated in the application and does not change the
existing prepared launch build, the original installed provider or the user's
profile. The next milestone is the dedicated room worker and shared-socket actor
integration, relevant network/identity policy, discovery and the existing chat
page binding. Saved nickname/options compatibility and managed-room receive
policy also remain. No real messages were posted and no live profile was used.

Work pauses after this milestone at the user's request. Resume from the shared
UDP boundary; do not repeat passed coexistence tests absent a relevant change.

Implementation: `4ff478031095830fcfb7531dd6bc0f21b30906af`.
Provider-overlay tree: `f638d44ec8f30f47a3bfdfa96dd91f51aa3ef338`.
