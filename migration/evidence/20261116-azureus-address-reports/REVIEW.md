# Original Azureus external-address reports

Native source `5859121dadff536dea760ddb5b9554a2bf309c88`, tree `a63709264cce6c633a0dd92ca06fab28cb4ec6bc`; upstream
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Actual original DHTTransportUDPImpl.externalAddressChange supplies230 isolated
fixed-clock cases: IPv4/IPv6 family filtering, same-address/reporter handling,
contact-history threshold32, one initial30s deferred request, five/ten-minute
limits and Java signed-long elapsed arithmetic. Native matches these decisions.
The helper sets address_changing before the admitted original background thread;
no external-address discovery, network acceptance or timer execution is claimed.
Original site-local IPv6 is fec0/10; its ULA/unspecified/multicast decisions are
recheck decisions only, never permission to advertise such an endpoint.

The real UDP actor mints a sealed remote claim only after pending-RPC peer,
transaction, connection, network, expiry and full error-body validation. Its
bounded queued observer is independent of optional diagnostic collection, counts
packet-observed contacts only and is revoked by observer replacement or close.
The receipt contains an untrusted reported endpoint, not a local binding or
source authority. Actual IPv4/IPv6 UDP responders verify correlated delivery;
wrong source port/transaction/connection/network, unrelated error type, expired
requests and queued delivery after replacement/close do not deliver claims.

The first fixture setup incorrectly assumed a registered peer, then consumed
an extra maintenance PING. Both fixture failures are excluded. Final setup uses
an explicit actual loopback endpoint and no synthetic observation. The focused
wire executable passes in under one second; compilation output stays private.

This is the report-policy and observation prerequisite. Normal-owner independent
address confirmation, automatic runtime rebinding, retained DB values and fresh
router/session ownership are the next integration chapter. They are not claimed
complete here. No live profile, payload, running client, current daily or SDK
was changed; no full F09 baseline is advanced.
