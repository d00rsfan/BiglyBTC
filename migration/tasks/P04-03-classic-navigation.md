# P04-03 Classic view navigation

State: first My Torrents navigation slice implemented and tested; broader
navigation remains in progress. User requested continued autonomous implementation on
2026-10-18 while away, specifically prioritizing **View > My Torrents** and
**Ctrl+1**. This continues P04-01; full client parity remains open.

Source baseline: BiglySoftware/BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
`MainMenuV2.buildSimpleViewMenu`, `MenuFactory.addMyTorrentsMenuItem`,
`MainMDISetup.setupSidebarClassic`, and original localization/keybinding code
define the contract. Classic English resolves `library.name._classic` to
**My Torrents**, whereas modern mode resolves it to **Library**. The first
preview incorrectly used the modern title. The classic main torrent entry is
non-closeable and navigation activates its stable MDI identity.

Implementation workspace: `~/Java/BiglyBTC/preview/native`. Source changes and
validated public milestones belong in that sibling repository. Research and
unresolved questions remain durable here. Original Java/client/profile are
untouched; generated offline sample profiles remain isolated.

Initial deliverable:

1. Correct the main tab/header title and restore original View menu order.
2. Activate the same My Torrents page through the menu or Ctrl+1 from table,
   filter or Files focus, preserving filter/selection/model identity and avoiding
   duplicate tabs. Do not recreate a transfer service on navigation.
3. Preserve Ctrl+2/3 labels for My Tracker/My Shares with explicit availability.
   These original services represent hosted torrents and shared resources, not
   downloaded torrent tracker URLs or a file-list alias. Do not enable a
   misleading service implementation.
4. Preserve existing units/smoke and add actual keyboard/navigation regressions.
   Stage the dist folder only after checks pass, record actual results and
   immutable implementation hashes, then commit/push the bounded milestone.
5. Continue with source-backed classic usability work: useful existing data,
   layout/navigation/keyboard behavior and compatible isolated preferences.
   Independent source reviews determine ordering; record unresolved work rather
   than broadening completion claims.

Evidence directory: `migration/evidence/20261018-navigation`. Source review,
test implementation, production UI and actual build/publication are separate
tasks; root owns builds and publication. Do not reinterpret historical frozen
research inputs as qualified by these UI checks.

## First implemented navigation milestone

Public native commit `9abef333ab0a4178bc68e098a038d04514a37ddc` restores the
classic My Torrents name, original initial View-menu order and Ctrl+1 activation
of the retained main page. Local GCC16 build and all24 native unit checks pass,
including13 navigation Qt rows. Actual generated-profile smoke passes with
unchanged inventory/cache/state bytes and no payload creation. Both actual
GitHub-hosted desktop run39110452616 and SDK run39110452580 pass on that commit.
The updated `dist` launcher is staged with rollback retained; an older native
preview window was deliberately left running, with all82 runtime dependency
files proven byte-identical during folder promotion. It shows its old in-memory
UI until relaunched. This is not a claim of newly observed compositor behavior.

My Tracker/My Shares retain their original labels/shortcuts but are disabled.
The source audit identifies viable pure saved-config projections as follow-up;
manager/network/scan initialization is outside that read-only boundary.
Per-pane column controls are the next implemented usability slice undergoing
source review and actual checks. See the evidence rather than treating the
original-source design proposals as passing runtime results.


## Source correction: classic main-tab closeability

The later full constructor/virtual-method trace in
`migration/evidence/20261018-navigation/classic-closeability-correction.json`
corrects the earlier noncloseable-original inference. Original az2 creates main
TabbedMDI; TabbedEntry forces closeability true despite the false factory argument.
Ctrl+W closes the selected view, with the generic entry veto; Ctrl+1 can reopen
My Torrents. The native permanent tab in the earlier preview is an adaptation,
not original parity or an accepted final restriction. Preserve the old source
reports unchanged; P04-07 now corrects the native visible behavior.


## 2026-10-29 bounded follow-up

The normal launcher now uses the same narrowly proven Qt6.12.0 Wayland no-focus warning filter as preview; explicit diagnostic/fatal/logging controls bypass it and unrelated messages retain the original handler. General focus/IME acceptance remains open.

Qualified source: `3228091a1e12f81b334e27e589da8605c0b76420`. See `migration/evidence/20261029-tracker-tiers-tor-console/REVIEW.md` for the initial failures, repairs and explicit reuse of identical tested executables.
