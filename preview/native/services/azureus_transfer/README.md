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
