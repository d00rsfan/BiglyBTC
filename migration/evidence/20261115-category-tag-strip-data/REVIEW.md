# Classic Categories/Tags filter policy

Native `2f6fbc0357d7fd376be142cc10b6cb064b80dc23`, tree `64c093dd5b4bcd9c2280daaf51b14dbfbcfb6628`; BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Policy/data foundation only;
the visible strip and settings/command integration remain in progress.

Original SWT settings/defaults and Long.intValue coercion are retained. Pure
settings edits preserve unknown fields, untouched original integer encodings and
reject stale expected values. Original click/Ctrl/All behaviour, empty selection,
union/intersection, category resolution and current-library full-hash membership
are exercised. Category and manual identities are distinct; names do not grant
membership. Invalid/ambiguous source rows do not acquire invented membership.

The classic AZ2 panel includes hidden manual tags, while TagStatsView excludes
them. The sealed membership factory now has explicit All/Statistics modes; the
existing default remains Statistics. Statistics presentation admission rejects an
All-mode join. The panel consumes the separately sealed All-mode join. Source
metadata is read from owned original bytes, rather than trusting edited public
projection fields. No extra per-torrent state file read is introduced by this
pure module. Original group/name ordering and category placement are preserved.

The unchanged actual FormattersImpl comparator matches512 generated comparisons,
including numbers, zero prefixes, case and Unicode. Actual TagUtils sorting plus
the original UI category-placement rule matches seven generated category/group
rows. Existing manual Statistics tests pass. Only pure targets were compiled and
checked; no full application build or live desktop qualification is claimed here.
No profile, preview, SDK or launcher changed. Continue the visible strip without
pausing and batch its complete Host/UI integration before a full build.
