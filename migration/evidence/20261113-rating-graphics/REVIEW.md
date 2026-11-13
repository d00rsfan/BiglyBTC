# Original rating graphic cells, sort controls and mouse actions

Native8fc78626a14bdac8b39072f35b815bbe04d6102c, tree
162ff0ff8aa23fe5b0aed8fb3f5f27f6de5f295e. Original azrating
1629340340d0bc391d9e582f9ee989a93d550581; bundled test-only SWT artifact from
BiglyBTcb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Original source RatingColumn,
RatingImageUtil, RatingSWTUI, RatingResults, RatingsUpdater.getRatingsForDownload
and English Messages.properties are inspected. Source/body/artifact digests are
in[source-scope.json](source-scope.json); original icon digests/licence are in
preview/native/ui/assets/rating/NOTICE. No SWT binary is shipped with Native.

Both classic tables paint the original65x15 images: raw replacement of the
rated portion over the unrated pixels, original alpha and the two-row personal
rating line, including light/dark colours. Wide cells retain the original size;
narrow cells reproduce SWT's16.16 endpoint-aligned nearest sampling, including
one-pixel cells. Qt's default nearest scaling maps different source pixels and
is not used. The column starts at the original95-pixel default while saved
width/order/visibility continue through existing preference restoration.

The original average fallback, float count/10000 sort addition, global/personal
sort roles and English summary tooltip are preserved. Accessible text remains
available while the delegate paints graphics. Header radio choices share the
original ratingplugin.col.sort setting across both tables, default0/Java-int
narrowing. The dedicated config writer normalises a selected mode to0/1,
preserves unknown fields and uses the original full-file CAS/recovery policy.
It does not mark a table layout edited or rewrite column geometry.

Middle button press uses the original score calculation and preserves the
current comment. Ratings above5 clamp through the original constructor policy;
negative padding results remain supported, and a too-narrow division is refused
without a write. Double-clicking the rating opens its editor for that exact
clicked torrent, independently of the normal torrent-detail double-click setting.
Backend requests retain profile/session/row generation and source checks, and
stale requests cannot overwrite another row or profile. No implicit dialog
comment clearing or forged editor token is used for a quick rating.

A real defect found by the new test was fixed: saveAttributes invalidates an
original cache.dat entry, so reading rating cells only from that cache lost the
personal value after an edit. The table now uses the existing PrimaryPresentation
metadata snapshot and its device/inode/size/mtime/ctime freshness checks when
that cache row is absent. The same already-read attributes supply other columns;
ratings introduce no separate whole-library file scan or independent stale
cache. An earlier local fallback-cache draft was removed before this commit.
The real backend test now observes the saved personal sort value after refresh.

4224 original-method pixel cases use the real headless bundled SWT ImageData
PNG/pixel/scaledTo implementations, with only Image/Display and dark-appearance
wrappers.1430 more compare the exact selected original click calculation,
including narrow cells and padding. These are actual configured Java/SWT
comparisons; the native cell-number/tooltip regressions remain source-inspected
plus native tests, not a full live SWT table/locale claim. Pure pixel comparison
initially passes in1.09seconds; the extended pixel/click suite also passes after
integration. Unknown/non-Public providers and full plugin lifecycle parity are
not implied by this Public-first UI chapter.

Local-rating host/UI tests pass in0.37seconds. Real backend and both-table GUI
wiring pass after fixing the cache defect and correctly waiting for each new
asynchronous editor request (1.56seconds). The final three header/layout/wiring
groups pass in3.07seconds. A focused GUI scenario at200% passes three Qt rows
with zero skips in0.533seconds. Screenshots at[100%](columns.png) and[200%](columns-2x.png)
use generated rows and were visually inspected. The static UI/desktop host and
production main_desktop.cpp object compile; no new runnable preview was linked.
No fresh sanitizer/platform/full real-profile performance credit is claimed.

No live profile, running frozen process/image, SDK or user-facing build changed.
Continue the[per-download chat adapters](../../tasks/P06-DOWNLOAD-CHAT-ADAPTERS.md)
without a chapter stop; non-Public providers/subscriptions remain lower priority,
browser and media player remain last.
