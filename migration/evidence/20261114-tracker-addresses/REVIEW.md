# Tracker source sockets and actual announce addresses

Native `93976d608a5f893ebe2010bf886a6cfa9c2cf4ad`, tree `e39c6c3d1535d9e23703960337e73b64161738c8`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
TRTrackerBTAnnouncerImpl request construction (explicit IP overrides are
optional). Frozen shared UDP SDK remains
`8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3`.
Its established source/patch provenance is in the preceding network diagnostic
and selected-v2 reviews; no SDK source or binary was modified here.

Inspection displayed SDK local_endpoint, the local connection source, as though
every interface row were an active announce route. It included endpoints whose
SDK enabled flag was false. Disabled endpoints are now omitted; remaining rows
say Local source socket and explain how NAT lets a tracker observe the public
source. IPv6 socket strings are bracketed. Stable column IDs and tracker URLs
are preserved. A legitimate private LAN source remains usable.

The Native engine does not set announce_ip. Actual frozen SDK capture passed
eight owned HTTP/UDP cases across IPv4/IPv6 and public/private v1 torrents:
HTTP sends no explicit ip/ipv4/ipv6 local-address parameter, and UDP's IPv4
address field is zero, allowing tracker source observation. Generated RFC1918,
link-local IPv4/IPv6 and ULA predicates also passed against that SDK. The SDK
only builds extra explicit public IPv4/IPv6 lists for non-anonymous private
torrents, excluding local and loopback addresses. These loopback wire tests do
not measure the user's router mapping or every address/proxy configuration.
There is no evidence that the reported local source was exported as a public
peer address. Hardcoding an external NAT address as a local bind is unnecessary.

Packet checks passed in21.03s; the focused Qt tracker table/header/cell tooltip,
stale-packet and resize test passed in1.27s. An old hardcoded15-tab assertion
failed because the existing full build has16; it now checks required tab names
rather than a feature-dependent count. New SDK fixture compile corrections
matched the exact torrent_info span constructor and use Qt SHA1 for generated
payload metadata. Production waits and SDK behaviour were unchanged. Desktop
build passed119.88s; fixture-only UI build37.12s. Same GCC16/C++23/Qt6.12.

[Generated fixture screenshot](trackers-source.png) contains no profile data.
The new frozen preview has SHA256 `b07bc6535cfe44191e7f1e60f01e38a41d45a4ca2ee17f06fa5924960ca16aa0`
(21966984bytes), reuses the existing SDK/Qt and makes no
profile backup. Real-profile/live NAT acceptance awaits user launch; the running
previous process has not been restarted. Continue private window work after
readiness notification, keeping the live acceptance task open.
