# Public chat moderation and related channels

Native `40fdcbe135afcedc9d2b2071b3c825ab38164c21`, tree `f62bc978b479d26a4aa5b957ba55d41372a98fba`. BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`. Selected Public chat controls are implemented and tested; this does
not declare complete chat/Friends or whole-client parity.

## Original moderation behaviour

Spammer and Not Spammer use the original labels and five-message threshold.
The actual retained signed roster, full public key, room epoch and current view
authorise the action. The profile IO gate rechecks the saved original identity and
live room token; completion rechecks the actor's expected runtime state. Original
participant props, legacy Pin and unknown fields survive. The saved spammer flag
is deliberately not restored as runtime state, matching the original constructor.

The previously qualified sixteen-record history policy is connected after valid
new ledger admission. Duplicate, rejected and imported records do not feed the
live spam filter. Direct public-key equality alone is not a relay ban. Signed
messages remain available to Bloom/relay synchronisation. A local error report is
bound to full sender/signature and saved as per-message `l` in encrypted history.
An unmarked relay author can produce a displayed local error; these are not all
silenced as an invented policy. Marked senders are hidden by the original ignored
union. Notifications and mentions use the displayed local report.

The actual session captures its dispatched request's busy-channel count, adds
four bytes from SHA1 of the RPC relay contact only when that count exceeds128,
and retains the original80-byte history bound. Java-style expanded IPv6 contact
formatting is preserved. The owned transport regression changes the count between
dispatch and completion and checks that the earlier request's policy is retained.
Unsigned message hints cannot choose that relay contact.

## Related channels and settings

Managed/read-only keys follow `parent[pk=BASE32(pub)]` and
`parent[pk=BASE32(pub)&ro=1]`. The held IO gate rechecks the fresh saved parent
identity and expected manager before creating a child. The original parent signer
is reused for its manager channel. An independent guest identity receives but
cannot post in the read-only channel. Menus reject old epochs and exclude private
and already managed parents. Global, separate and download chat views use the
same guarded gateways. Pin is not classic Friends.

The Swarm Discoveries editor now retains imported2M and larger positive original
configuration integers, matching the capacity foundation instead of clipping the
setting to a storage resource cap. No running profile value was edited.

## Validation and remaining scope

The grouped full application build passed, followed by nine focused CTest groups.
Affected menu and relay tests were checked again after their final changes; the
new owned-network moderation/manager/guest scenario passed in35.27s. It uses only
generated profiles and owned loopback peers. The underlying actual unchanged Java
spam-policy fixture qualification remains198 cases/9860 decisions; it was not
rerun merely for unrelated UI edits. See qualification.json for individual groups.

Only actual changed targets were rebuilt. No unchanged long large-cache workload
was repeated and no delivered image, launcher, live profile or shared SDK changed.
Classic Friends were deferred by the user's explicit priority answer. Automatic
flood policy and global IP-filter bans remain separate later work. The4096 marked
history-key resource cap remains explicit. Real desktop/router acceptance is still
manual; no full-network/UI performance claim is made. Next priority is the original
Categories/Tags strip above both torrent tables.
