# P04-07 Classic Close Tab and saved-view readability

State: verified bounded native source `4bfa2d85c9154ac3ede33c8298d2d7209d6589c5`
is committed/pushed and dist is updated. All32 native units pass (zero skips),
including16 Close Tab Qt rows. Three lifecycle suites pass leak-enabled ASan/UBSan,
and full three-view/close-all/Ctrl+1 smoke passes100/150/200%. Hosted SDK and desktop both passed; the actual hosted32-unit report and
three-view captures are retained. Original baseline is
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Contracts: `classic-close-tab-contract.json` and independent
`classic-closeability-correction.json` in navigation evidence. Original classic
main My Torrents IS closeable: main TabbedEntry overrides the false factory flag.
The initial audit missed that virtual-method override. Old reports/hashes remain
preserved, with this explicit correction. Close Tab/Ctrl+W targets the view,
separately from Close Window/Exit, and supports a generic source close-veto hook.

Implement File Close Tab/Ctrl+W for all three known native pages, using the same
handler as tab close buttons. Remove the temporary permanent-main restriction.
Closing My Torrents retains its owned widget/model/filter/selection/details,
without a visible tab; Ctrl+1/Ctrl+F reattach that same screen. This is a native
preview lifetime adaptation, not an assertion of Java destroy/recreate/plugin
veto/persisted-view parity. Closing a view must not stop/cancel work or exit the
window. With zero tabs, target actions/Refresh are disabled; navigation can reopen.
Existing safe generation/latest-pending behavior and hidden-action gates remain.
On reattach, legitimate missing/stale read-only metadata may load through the
existing details path; retained current details should not trigger extra work.

Separate reversible readability adjustment: size saved-view headers and known
Record/Type labels once using font metrics; bound other widths and retain manual
resizing/horizontal scroll. No saved-row scans or refresh-time automatic sizing.
No model/role/state/persistence changes, and no original column-setting parity.

Before publication: source review, dedicated actual keyboard/tab/menu tests,
updated previous tests correcting the permanence assumption without dropping
selection/stale-target assertions, full native units, generated three-view smoke
at all scales, relevant sanitizer lifecycle tests and visual inspection. Root
owns actual builds/execution/commit/push; agents supply separate implementation,
fixtures and independent source review. Full plugin close-veto hooks and original
view/persistence/tray/window lifecycle remain OPEN prerequisites.


Exact source/evidence/distribution hashes and all view captures are in
`migration/evidence/20261018-close-tab/verification.json`. Root independently
rechecked all20 corrective original source blobs/current files and the original
user staging index.103 dist files verify;82 runtime files are byte-identical to
the predecessor retained at native `build/close-tab-20261018/previous-dist`. No
process was signaled, normal profile opened or package installed. Initial field
labels/type text are visibly readable in reviewed offscreen captures; saved paths
and timestamps may need manual column resizing/scrolling. Real compositor and
complete original view lifecycle semantics remain OPEN.


Public verification commit `efdf27688594ceecd6d27f8bc2c6e7c0e4c5ce4c` is tagged
`milestone-classic-tabs-20261018` (remote annotated object
`9e3f775ae1e0ced3947b0ca89f502097efffa6a3`). Desktop39116079560 and SDK39116079517
both completed successfully for exact source4bfa2d85c9154ac3ede33c8298d2d7209d6589c5.
