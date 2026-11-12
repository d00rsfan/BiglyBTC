# Current public network information in Statistics

Native `bb18d023d0b200587f4e4ebf93dca06bb7b90db1` / subtree `5687071fdd0a58cb1fd0761fa5150f70a29c4709`. Original core
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; scoped TransferStatsView.refreshCapacityPanel,
NetworkAdminASNLookupImpl/ASNImpl and ASN Details prefix matching. Existing SDK
upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b`, overlay parent
`4ff478031095830fcfb7531dd6bc0f21b30906af` unchanged. Whole NetworkAdmin and automatic
speed-management behaviour are not advanced by this display milestone.

Statistics > Transfers > Capacity now begins with Current public network:
separate IPv4/IPv6 address, ASN, network-operator name, BGP prefix and status.
Names are the ASN operator registered for that route; retail ISP branding may
differ. Text is selectable and plain, never executable HTML.

Addresses come from existing SDK external-address/listener alerts or the already
validated shared Public DHT endpoint. Wildcard/LAN/loopback/ULA/mapped addresses
are not relabelled as public. No extra IP checker, torrent tracker, payload probe
or permanent JVM is started. The UI shows Not detected yet until actual public
addresses are observed. Current data clears on session/address changes.

The asynchronous resolver follows original Cymru TXT queries: reversed IPv4 or
IPv6 nibbles under origin/origin6.asn.cymru.com, then AS<number>.asn.cymru.com.
Numeric ASN and prefix/address identity are validated before display or use in a
query. Original ASN Details is read without rewriting configuration and used only
when its prefix matches the observed address. Cached/lookup status is explicit;
a previously saved network is not copied onto a different address. The resolver
limits two family slots, five seconds per stage, retry after one minute and16
memory cache entries. IP/session changes cancel pending objects and suppress late
replies. Isolated/proxy policies disable fresh direct DNS. DNS failure affects
optional display only, not essential counters, queue work or torrent traffic.

## Validation

Four affected current-image CTests passed (full logs remain ignored/local):
statistics-current-network-identity, statistics-page-behavior,
saved-speed-manager-ui and general-chat-profile-publication. New checks exercise
actual owned UDP DNS TXT replies for both families, operator lookup/display,
wrong-prefix/name/ASN responses, a delayed old-address response, session close,
timeout/retry bounds, private-address and DNS-policy denial, matching saved cache,
and original Java query/parser parity including UTF8 names. The real Host/Bridge
publishes the enriched packet while refusing to identify loopback fixtures as
public. JDK17 runs only the generated test oracle; no live JVM dependency exists.
Application and affected test targets built in the existing Qt/GCC cache. No
sanitiser run or real-profile/network-operator acceptance is claimed here.

## Runtime blocker and next work

The user reported recurrence of the earlier fatal automatic Checking problem
while this milestone was being built. It is separate from optional network
statistics. Native was closed by the user; a small private read-only diagnostic
capture was made before Java was allowed to repair the library. No personal
names, paths, hashes, addresses, queue identifiers or traces are included here.
The current image is built but not promoted for a real-profile retry. Continue
the Checking regression immediately; All Peers stays deferred. Do not interrupt
Java or alter its profile/payload while it repairs the library.
