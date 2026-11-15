# Classic Categories/Tags chips

Native `bbb60257fba3f3749f6399ba09491fd686c42e38`, tree `eb00ac238776139fa4a61d205bbe53c19ba1c653`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. This completes the classic strip
chapter using the existing supported tag/category services; complete tag-engine,
Java/SWT pixel or whole-client parity is not claimed.

## Behaviour and original settings

Both torrent tables have independent filters combined with completion and name.
Original `Library.ShowCatButtons`/`Library.ShowTagButtons` preferences control
visibility, completed-only placement, filter-only tags, alignment, image override
and the shared inclusive/intersection mode. Normal click replaces selection;
Ctrl adds/removes; All clears other choices. Removing the last selected chip
restores All. The original empty-set predicate is retained for explicit empty
sources but a normal last-chip click does not leave an empty selection.

Correction to the policy foundation: its initial click test removed the last
choice to an empty set. Further audit of removeTagFromCurrent established the
All fallback; this source and regression fixtures correct that earlier claim.

Categories use original definitions and actual saved assignments. Tags use
stable full identities and a separately sealed current-library hash join; names
alone cannot grant membership. Classic AZ2 includes hidden tags. Statistics keeps
its original separate visibility/diagnostic mode; missing original member lists
are valid empty tags for the classic strip. Natural group/name sorting and
category placement match actual unchanged original Java methods:512 comparisons
and seven generated sorted category/group rows.

The source cache reuses unchanged definitions and exact hash inventory. Rendering
adds no per-torrent state-file read. Membership updates keep the button objects,
selection and table viewport; wrapping is bounded and does not force window
height. Local image-file overrides are bounded and fall back to text. The
[generated screenshot](strip-generated.png) contains synthetic data only.

## Assignment and profile compatibility

Internal torrent dragging,500ms hold and a context action use one bounded command
for the selected torrents. Drag data is bound to the source profile session;
external/fake sources and old profile drags are rejected. The real Host rechecks
session, request replay, model revision, actual targets and pending metadata before
filesystem effects. The helper freshly resolves every target in the actual
original inventory before writing. The original add-all-if-any-missing, otherwise
remove-all rule and automatic-tag direction checks are retained.

A manual-tag group edits original o/p lists in one pass and one tag.config commit,
preserving member order, unknown properties, signed addition times and unrelated
fields. Duplicate/invalid selections refuse. Categories use the existing per-file
CAS and owner rebase. The library refreshes once after a group; partial completion
is reported rather than pretending that committed work was rolled back. Generated
checks retain inventory, payload and resume bytes and acknowledge changed owners.

Existing backend limits still apply: unsupported group exclusivity, rates,
actions, tracker rewrites and other membership effects refuse with source kept
for their later engine integration. No new effects were invented by the strip.
Built-in image IDs remain part of wider native asset coverage; this strip supports
configured local files. These broader service limits remain in the migration plan.

## Validation

Full application build passed in394.14s. The final UI correction/relink passed
in54.33s. All seven scoped groups and the targeted Statistics/Ctrl-1 navigation
case pass; individual timings are in qualification.json. Existing single-member
and store tests pass after the linear batch implementation. Actual grouped
profile writes, before/after resume and payload comparison, one tag-file commit,
known partial completion, old menu/profile fences, ampersands, Ctrl selection,
button reuse and160-chip height bounds are exercised. No live profile was opened
or written by qualification. SDK unchanged. Long unchanged network/cache suites
were not repeated.

A real legacy regression found by classic tests was fixed: replacing General by
its scroll container now preserves its initial selection. Old test assumptions
about thirteen byte-labelled columns and a fixed total of Statistics tabs were
updated while retaining source/selection/navigation checks. The final fixture
requires current user-facing columns and human-friendly headers.

The user requested one daily build after this chapter, then autonomous
continuation. Real desktop/drag and current-profile acceptance remain user checks;
preparing the image does not restart the client or copy its profile.
