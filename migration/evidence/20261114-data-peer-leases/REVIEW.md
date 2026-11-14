# Finite DATA peer authority for NAT and declared/physical routes

Native `f79c88d6f19040dd352dec95944b91a38915f1e8`, tree `5ffdc825a2ee7d2abaaa99acd3b21b1ab027fe94`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

A trusted source owner can grant a physical peer access to one registered DATA
handler/receiver, expected external address/version and source revision for at
most120s. There are at most64 leases. These leases never populate the ordinary
peer/routing/contact/cache sets and cannot send PING/FIND_NODE/STORE. Renewal
invalidates the previous receipt; foreign/default tokens cannot withdraw it.
Withdrawal cancels only related DATA state; unregister/close removes its leases.

Unknown-source DATA is structurally queued only with a live matching lease;
actual assembly/reply/send rechecks its source revision under normal preflight.
The final PublicBound physical writer accepts scoped DATA state/revision only
from its private DATA path; ordinary writes retain their existing peer gate.
This does not weaken the SDK witness or invent an external observation. Explicit
aliases on already-admitted peers also require the matching lease revision;
ordinary handlers retain exact-originator behaviour without a lease. Revoked or
expired aliases revert to that default. A route geometry result grants no
implicit authority to other handlers, rooms, private identities or DHT requests.

The existing NAT actor can use these DATA-only peers instead of requiring global
DHT admission. New owned SDK IPv4/IPv6 three-party cases prove control exchange
with no ordinary peer admission, as well as original identity retention. Tests
cover handler/receiver/IP/version/source/deadline limits,64-entry bound, renewal,
foreign receipts, expiry, withdrawal, source changes and handler close. A normal
Call handler demonstrates an explicit alias on an already-admitted peer and
refuses it after source change. Actual source-hook assertions remain enabled.

Ten actor test entries passed in3.36s and four retained shared UDP entries in1.30s.
The final targeted build passed in72.4s. New actual SDK tests use OwnedLoopback;
they do not claim new physical PublicBound/NAT-router acceptance. The final
PublicBound gate is part of the source integration and remains subject to that
acceptance gate. Declared-port seams preserve the actual SDK send/receive/current
witness and do not modify Provider::channel. The existing pinned SDK is unchanged.
No application first-contact grant or automatic relay activation is included in
this commit. The following step connects bounded NAT ingress to the owner.
Full original transfer/contact references remain prerequisite evidence; they
were not rerun as combined Java interoperability. No separate reviewer was used.
