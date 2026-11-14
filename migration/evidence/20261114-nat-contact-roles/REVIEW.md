# Original external and actual transport contact roles

Native `d94740235671a6dc596ecbbb2eef2201b5734d55`, tree `af27a3b12482f62196bf07fc6f23ad9380891f97`. Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

RoutedContact now represents the original split between declared external
identity and actual transport endpoint. Its ID and exported contact derive from
the external address. A different transport port on the same IP preserves both;
mapped addresses canonicalise as Java does. Bindings accepts a routed contact
and retains both initial external and physical roles across rebind. The bind
reply reports external port; relay connect/punch will use stored transport port.
The prior external-only binding overload remains for existing memory callers.

Geometry validation grants no source/peer authority. The eventual owner must
obtain the actual tuple from a fresh source/receipt gate and keep network/lane,
handler/type/deadline and signed private identity separate. Native withTransport
refuses a different IP instead of cloning the unrestricted Java setter. Invalid
source geometry fails before warming the external ID cache.

Six QTest entries passed in0.38s, including56 comparisons with the original
constructor, addressMatchesID/getAddress/getExternalAddress/getTransportAddress/
setTransportAddress and getNodeID. Tests cover both families, protocol32/33/50/55,
different actual ports, same-IP transport updates and foreign-IP invalidity.
The constructor name and DHTUDPUtils reference are adapted to the fixture;
network-position creation is a no-op, contact transport is a memory object and
ID history a memory map. Original SHA1Simple and selected constants are recorded.
Native tests also exercise export/ID stability, canonicalisation, invalid tuples
and retention of the first physical endpoint after rebind. After the shared
role change all four affected targets were rebuilt together; retained30client,
75binding and100wire traces passed again. [Qualification](qualification.json),
[exact source scope](source-scope.json).

This completes the contact model prerequisite, not NAT packet admission or
actual tunnelling. Next implement source-scoped NAT handler/external-vs-physical
DATA handling, expected early tunnel observations and a bounded shared-UDP
three-party actor, followed by lookup/publication and chat integration. Incoming
tunnel6 may precede the relay PUNCH reply; the owner must retain that observation
under the pending source-bound round. Whole NAT/private chapter remains open.
The user executable, SDK, launcher, actual profile and public network were not
changed or used for this qualification.
