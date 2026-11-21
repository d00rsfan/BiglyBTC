# Required live private source transaction before Host activation

The bound-source API renews SDK/source evidence only. It does not rebase private
source snapshots or restart a withdrawn transport. SourcePermit::validate
permanently revokes on stale proof/stamps,so quiesce its actual worker before a
legitimate tag write. Stop on private Qt,ack any Executing permit,join outside
Qt and filesystem gates;retain actual router/session/torrent/storage lifetimes.
A revoked source must never be resurrected.

An owned refresh batch must include every retained nonrevoked prepared private
child under the same root. Capture fresh bound proofs on the original Host IO
worker,with all SDK waits outside filesystem gates. Under one trusted source
turn,validate exact original profile/key/config/inventory/state/storage and all
named identities. Re-evaluate only tag stamps against fresh actual manual source
and sealed per-child SDK proofs. Prepare every new immutable privateRates value
before publishing any;publish the full batch and exact authority revisions only
after all unrelated snapshots remain unchanged. On concurrent source change,
abort/hold rather than bless new metadata. Restart source validation after full
publication;test Ready/Executing races and unrelated metadata replacement.

All deliberate tag writes matter,including unlimited last-tag removal and
non-rate edits that replace tag.config. Current Host mutation sites are
session_bridge.cpp manual-tag dispatch,tag_assignment_host.inc chip/menu paths,
and tag_constraint_host.inc automatic producer/batches. Existing normal effects
run after CAS/memberAccess release. Generic store/source CAS must stay exact.
Lifecycle commands for unsupported private routes remain separate from pure
rate-membership effects.

If pausing shared SDK sessions before CAS,do not revoke the prepared generic
writer proof before the CAS consumes it. Preserve already-paused SDK sessions
and pair recovery on success/refusal without resuming user-paused handles.
No SDK/source wait under the SourcePermit-held gate.

Host root lifetime belongs after held lease and before PersistentSession field
so fallback engine destruction precedes root destruction. Host destructor
explicitly joins Mixed/private services first. Private-only initialization skips
normal PersistentSession,so root must exist independently. A malformed tag
source must not prevent opening the UI for repair:consider a separate deferred
root constructor that captures only actual startup/profile ownership and leaves
all SDK admission/effects to later synchronization. Keep existing strict open
and generic writer behavior unchanged. Pending metadata,promotion,Mixed,actual
private transport and common global/LAN still require full normal integration.
