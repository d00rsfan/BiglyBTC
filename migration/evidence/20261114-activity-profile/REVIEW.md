# Original internal notification profile foundation

Native `e1fa047ebe7694e5bb1d133011e0448fe1221033`, tree `2f4ae5b502ed76cc154ca7e62f62c02e8b53c580`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: LocalActivityManager,
ActivitiesEntry and ActivitiesManager source audit. This is the internal
notification centre, not an OS notification/D-Bus adapter.

The independent ActivityProfile adapter reads/writes VuzeActivities.config
version2 through existing ConfigFile/OFD compare-and-save. It preserves unrelated
root values, LastChecks, entries, unknown fields and opaque bytes. Actual Public
chat records use the original UID (Public: plus room key), LOCAL_NEWS_ITEM,
image.sidebar.chat-overview, actions, callback class and allowReAdd/net/key map.
A strictly newer same-ID update resets readOn/viewed; older/equal updates retain
the preceding record. readOn>0 is read; explicit unread is negative wall time.
Original two-duration-year expiry and missing time/read defaults are retained.

Selection uses opaque whole-entry SHA256 preimages, so an updated same-ID row
cannot be marked or removed using an old frame. Batch selection validates every
member before any write, while an unrelated row update leaves a valid selection
usable. Chat deletion allows re-add; generic deletion uses original timestamp/id
tombstones. allowReAdd tests presence even for stringfalse. Existing tombstones
still suppress matching new entries. Private post input is rejected before file
creation. The adapter grants no network authority and has no application caller
or visible setting yet; the later owner integration must additionally validate
actual signed Public attention, room epoch, fresh original identity and options.

Malformed unrelated callback entries are skipped/preserved. Ambiguous unsupported
format versions, recovery writes, stale selections and foreign-owned ID replacement
are refused without overwriting existing data. Non-chat entry/unknown data remains
available for future adapters. This foundation supports version2 only; it does
not silently downgrade or erase a future/legacy format. It does not implement the
whole ActivitiesManager cache/platform-news/UI subsystem.

The actual unchanged LocalActivityManager factory and ActivitiesEntry.toMap were
compiled from the pinned Java sources on JDK17, using isolated generated settings
and the existing ReferenceFence with no external network/audio/profile access.
The factory created a generated Public chat entry; only its timestamp was set to
a fixed value. Its canonical402-byte BEncoder output is committed as a fixture,
with the small caller source and full original/helper/fixture hashes. Native
new-entry encoding matches that output byte-for-byte. No stripped-down substitute
for the original factory/serialization was used. Other policy checks are native
regressions/source audit, not full original manager execution.

Eight generated profile cases pass in0.01s: missing/private-no-write,
actual original factory map, newer/older/read/view transitions, unknown field and
expiry preservation, delete/re-add/tombstones, stale-batch atomicity, foreign-ID/
version refusal, malformed-entry preservation, and read-only backup recovery.
One coherent new-module build took35.58s; test-fixture corrections were compiled
only for this module/test. The production client was not rebuilt for an unused
adapter. No live profile/process/image, launcher or SDK was changed.

Next connect real per-room notipost options, batched signed attention, the
internal notification UI and offline profile actions. Do not expose an inert
checkbox or restore private conversation labels as Public rooms. Sound overlap
and broader friend/moderation/provider/platform work remain explicit.
