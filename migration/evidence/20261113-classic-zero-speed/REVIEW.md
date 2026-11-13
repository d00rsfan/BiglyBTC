# Original zero-speed cell behaviour

Native `64ef3dc97e760f99b82bfbf7e399f9bc339b598b`, tree `8be735b05dc7e32a0a4ec882f48e62f093c2c399`, follows the original torrent-table UpSpeedItem/DownSpeedItem normal
zero/absent-statistics behaviour: empty display text and numeric zero for sort.
Positive rates keep their existing units/formatting and integer sort keys.
Both incomplete and complete classic tables use this same model. Optional
provider observations remain optional; this is display/sorting projection,
not fabricated traffic, saved accounting or command authority.

Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
uis/src/com/biglybt/ui/swt/views/tableitems/mytorrents/UpSpeedItem.java refresh
and DownSpeedItem.java refresh. The original stores0 for missing datasource
and leaves the normal zero-rate cell blank, rather than showing a dash or
literal0. Disabled-transfer sort-1/icons are a distinct original case and are
not reimplemented or claimed by this narrow zero/absence change. Peer-table
formatting follows a different original item and is not globally rewritten.

The32 UI QTests passed, including a regression matrix for both table groups,
both speed columns and both sort directions, with absent, explicit0 and mixed
positive rates. It checks real Qt proxy numeric ordering, blank zero cells and
zero telemetry updates. No full unrelated integration matrix was repeated.
Only the focused UI test target was built; no daily-use/preview image, runtime
manifest, application process or live profile was changed. The user confirmed
faster startup of the preceding runtime; that observation is not used as a new
performance-parity claim. Source-only autonomous chapters continue; a future
user-facing build waits for the user's request and the active chapter boundary.
