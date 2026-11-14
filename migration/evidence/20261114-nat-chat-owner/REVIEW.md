# Automatic NAT rendezvous and actual chat owner consumers

Native `471050e43c004e73d480a93fd2f2a74146fbc653`, tree `28a9e632cf423733e4744729794eaceee9bef705`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; original azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.

The actual Public chat owner now reads rendezvous records and attempts tunnels
concurrently with ordinary sync/private DH. Per-room/per-lane active bounds and
45/120-second attempt caches avoid repeated work. A fresh room permit renews DATA
routes, moves pending Calls and preserves the next DH phase's physical target.
Expected inbound NAT observations can move only already known canonical room
contacts. Private child activation includes its new epoch in the verified parent
frame; endpoint admission is DATA-specific. Full signed participant identity,
private policy and encryption remain authoritative. DH phase correlation uses
stable advertised identity/protocol and same physical IP across port changes.

Original saved Plugin.DHT.dht.reachable.NET defaults to true. The original
request-IP Bloom algorithm is retained:10001 bits, five wrapping Java hashes,
two filters rotated only on outgoing requests after strictly more than3minutes,
and incoming-address deduplication.60-second sampling uses the previous ten
closed minutes, with initial reachability retained until strictly more than15
minutes. Actual request codecs/network/source checks precede observation. Status
stays UNKNOWN until accurate. Only observed routable FIND_NODE contacts become
shuffled ping candidates; the first fresh success selects a relay. A failed
relay list is bounded to16. Own publication uses actual source-broker durable
versions, and changes/withdrawal remain restricted to the local NAT key. The
maintenance tick currently quantises the nominal1second probe spacing to2.5s;
publication gets a conservative hourly refresh. Persistent general remote-key
diversification and exact original DB republishing are not claimed here.

A generated unreachable profile on the actual application owner selected an
owned SDK relay, bound, published, and was found/punched by a separate SDK
reader. Actual owner/private tests passed in24.43s, room route tests1.42s,
NAT session tests4.87s, reachability/Call tests1.79s and directory tests3.12s.
The earlier owner integration stage passed40.48s/1.62s/4.6s. Deterministic tests
cover the strict15-minute boundary, address deduplication, Bloom rotation,
IPv6, positive thresholds and decay. Initial grouped build269.35s stopped on
an incorrect namespace in the new test fixture only; correcting it rebuilt
that target in49.35s. Production compilation had passed. Frozen SDK and
GCC16/C++23/Qt6.12; no sanitiser or public router NAT acceptance claim.

The optional original sync rendezvous hint, diversified directory keys and
pre-hard-stop withdrawal remain open. Hard stop still immediately revokes the
witness. Whole NAT/private chapter is not declared complete. No live image,
profile, SDK or launcher changed; all generated endpoints/data are synthetic.
