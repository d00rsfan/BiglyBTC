# Public Swarm Discoveries

Implementation `e45f797dd589666b2a2b8f5c7dc80deb659d9626`, native tree `e0c9c185faf672e8145ce3e4c795c30c66cce0e7`.
Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; aercm `cc18cdc42c36a33b435d78e3d86db64399058ac1`.
The scope is related-hash Public associations, original saved discoveries and
classic controls. Whole aercm/F12 and the migration remain partial.

Swarm Discoveries uses the existing shared UDP owner, existing Azureus0/3 and
optional BiglyBT4 lookup role. Publication uses Azureus0/3. It does not create a
permanent JVM or another listening session. Background association publication
starts after the original three-minute delay and continues in bounded cycles;
Discover now uses the same source-gated path immediately. Explicit related
lookup, secondary depth and optional large-file size associations are supported.

The original SHA1 association keys and bounded512-byte values use flags0.
Public source permissions are re-read before sends, result admission and local
serving. Known private sources, disabled RCM, non-Public, low-noise/metadata-only,
nonpersistent and unsupported selected-V2 hybrids are withheld. Stopped/paused
Public sources remain eligible as in Java. Tracker URLs and keys are excluded
from public publication DTOs; only original four-byte domain hashes are included.
Remote records already published can remain until normal expiry.

The original core `rcm.max_results` limit (default500), search depth, active-only,
DHT selection, naming and aercm source/UI preferences are preserved. Explicit
settings saves update core/plugin mirrors. Rank counts distinct observations;
read/unread, persistent exclusions and reset survive close/reopen. Cache limits
and filtered/private descriptors never cause a torrent download.

`rcm.config` remains compatible: legacy plaintext stays plaintext; encrypted
records use the existing `core.crypto.obs.id`, SHA1, RC4/drop1024 and gzip.
Missing/corrupt identity or cache fails without overwriting existing data.
Unknown envelope/root/row/config/deletion fields survive. Original cache links,
IDs and `rcmx.config` exclusions are retained; key creation is only for new caches.
The profile broker owns metadata I/O and CAS; no payload, saved resume or queue
inventory is changed by discoveries.

Tools/View → Swarm Discoveries shows names, rank, sizes, swarm counts, tags,
networks, dates, related hashes and all18 original RCM column IDs. Original
Table.RCM/TableDefault.RCM visibility/order/width/sort are read; changes preserve
unknown fields and other tables. Context actions search related content, mark
read/unread, remove or copy a tracker-free magnet. Trusted-source, text, unread
and related-parent filters apply locally. Settings expose the original limits.
Ratings remain visibly unavailable rather than invented. The existing torrent
intake workflow remains separate.

## Validation

9 unique affected CTests passed; see RESULTS.json. Four already-passing
network/cache suites retained identical executables and were carried; five final
suites cover the corrected UI/Host, async profile and extended dual-stack RCM.
The first batch found a partial-layout collision and an unbuilt test executable;
the corrected UI and missing test were checked in the final batch. Full compiler/test
output stayed in ignored local logs. GCC16/C++23/Qt6.12 Release O2 and the unchanged
SDK were reused; no new dependency clone, privileged helper or real profile run.

- Original Java cache/wire/crypto API → Native → original Java: UTF8/Cyrillic/emoji,
  rank/link/peer fields, unread, unknown fields and legacy/current formats.
- Actual Native shared-socket related-content exchange: bidirectional findings,
  save/read/cold reopen, queue bytes unchanged, no payload creation, private
  source exclusion and live global-permission revocation.
- Actual original Java IPv4/IPv6 UDP transports: Native SINGLE_VALUE association stored and
  decoded by Java; Java-generated association received/decoded by Native.
- Classic widget: original layout, numeric Size sorting, text/unread/related
  filtering, unknown-layout preservation and explicit limit change. Optional
  screenshot contains generated fixtures only.
- Real Host/Bridge: discoveries opens on the published profile, original limit
  is read and saved through the command broker, profile close clears the page.
- Existing shared Public chats and Azureus torrent DHT transfer/lifecycle paths
  were rerun because the same worker and socket carry discoveries.

## Boundaries and next work

This evidence does not claim live Internet RCM interoperability, a sanitiser run,
whole distributed keyword search, search Bloom/remote JSON/subscription adapters,
I2P RCM, publisher tag backfill/history maintenance or swarm payload merging.
These remain visible later work; browser/player and swarm merging remain low
priority. Java's probabilistic deletion Bloom filter is represented by exact
saved prefixes, avoiding extra false-positive suppression.

No real library/profile/payload was opened and the running Java client was not
interrupted. Earlier repaired-library acceptance, startup/steady CPU, unsupported
clean storage mappings and actual tracker HTTP400 faults remain separate open
items. Next is user acceptance of this preview and those blocking runtime faults;
no unrelated chapter was started. This chapter was implemented/reviewed by one
agent; no independent reviewer or new subagent work is claimed.
