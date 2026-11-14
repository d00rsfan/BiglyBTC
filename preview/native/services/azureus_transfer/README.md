# Original DATA transfers

Call and DDB Read retain their existing registration defaults. The NAT puncher
also uses `writeTransfer`: an empty request key on the same handler as calls,
with normal DATA fragmentation/gap recovery and transport ACKs. A successful
write means receipt of the bytes, not application acceptance or a call reply.
Only a Write registration or explicit `acceptEmptyWrites` on a Call handler
admits this mode. Read handlers and ordinary Call handlers keep rejecting it.

The queued receipt carries its source revision, endpoint, network and deadline.
The consuming owner must validate that receipt in its source gate before any
application effect; the source preflight permit is released before delivery.
Every physical packet already passes the existing fresh transport preflight.
Raw incoming receipts cannot be answered and stay bounded for exact replay ACKs
until expiry. Closing/unregistering retires them without a new application call.

Regression coverage: azureus_raw_write_tests.inc, shared_udp_raw_write_tests.inc.
The optional original Java case compiles unchanged DHTTransferHandler and exact
selected DATA body statements using tests/reference/prepare_raw_write_peer.py.
Its Adapter/header/contact seams use generated loopback endpoints only. This
qualifies the transfer prerequisite, not a complete NAT puncher or real NAT.

An explicit dual Call/empty-key Write registration can now opt into the original
same-IP external/physical contact-role split. ExactEndpoint remains the default;
Read and non-opted-in Call registrations cannot select this role. This is a
trusted owner API, not a packet-selected option, and grants no peer admission or
routing/STORE authority. The actual peer must already be allowed. Receipts retain
both actual socket source and advertised external contact; fragmented requests
must keep their first declared address/version. ACK/correlation still use the
actual physical endpoint, with the existing network/key/connection/source gate.
No operational chat registration has changed its default.

Owned SDK tests use a generated advertised-port header seam over an unchanged
real SDK channel/send/current witness. This does not invent an SDK-observed
external mapping or weaken Provider::channel. Source addresses, physical sockets
and IPv4/IPv6 deliveries are real owned fixtures. Unknown-peer NAT admission,
handler/source-scoped room/private route caps and the NAT owner remain next.
