# Passive selection and live context-menu corrections

Native bdfb9e4a5337f628be0a535b1ce2896b1e96c913, tree 968c77e5af9eac5412532bbcd0a5223a6212d699. Prior live table implementation
c5bc5faf67c0b053ac30b71f25ebe03339c57ca4. Original baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; this fixes Native UI lifecycle and
advances no integrated Java/component baseline. Engine/SDK/profile writers unchanged.

## Reproduction and implementation

An offscreen current torrent row was cleared and restored on every telemetry
update. Qt automatically revealed that row; the old guard then restored the
scrollbar. A generated regression observed two intermediate scrollbar movements
although the final position was correct. First run:2passes/1failure in259ms.

Preserve unchanged current/selection instead of clearing it. A shared guarded
transaction disables automatic reveal and painting before model notifications,
restores both scrollbar positions, then restores the preceding auto-scroll and
painting settings. QPointer guards deletion; no timers replay an old viewport
later. Both torrent panes and All Peers use it. Peer selection is restored by
model identity/index rather than a geometry-dependent selectRow call, retaining
multiple connections and the current column. Normal user navigation is restored
after each transaction. See [Qt autoScroll](https://doc.qt.io/qt-6/qabstractitemview.html#autoScroll-prop).

The context-menu regression failed for both single and multiple selections:
ordinary snapshots changed target generations, disabling all selected commands.
Keep the captured torrent identity set and profile session as menu intent, then
rebuild eligibility/targets from the current model revision. Source row reorder
must not retarget or invalidate an unchanged selection. Fresh permissions,
rendered-state/reentrant dispatch guards and aboutToHide lifetime handling stay
in force. Selection/profile changes, disappearance and revoked permission still
refuse commands. Global Start All/Stop All retain their broad revision fence.
No background library updates are frozen while a menu is open.

## Qualification and scope

Linux amd64, Qt6.12.0, existing Release build, synthetic UI data only. Build logs
stay local; no engine/network regression matrix or new sanitizer acceptance.
The application and three affected UI test executables built successfully.

- All Peers/live tables: 9 QTest passes. Includes Leech/Seed current rows above
  and below the viewport; telemetry, details, same-row snapshots and source
  reset; intermediate exposed motion; multi-peer selection through reset;
  keyboard Ctrl+End after passive updates; previous layout/rates/tracker controls.
- Library action UI: 31 QTest passes. Stable single/multiple context intent
  through repeated snapshots/reorder with latest submitted generations; updated
  Start/Stop eligibility; refusal on selection/profile/removal/permission change;
  existing busy/offline/stale-render/reentrant/direct-receiver controls.
- Scoped classic selection/context lifecycle: 12 QTest passes.

The initially attempted full classic suite failed at two older fixture
assumptions:13-column headers while the model already has33, and a presumed
blank point in the torrent table after opening a separate detail tab. That full
suite is not claimed green. The relevant classic lifecycle cases above pass.
Initial navigation test mistakenly used End (last cell in the same row); it was
corrected to Ctrl+End. The first reset-focused peer test exposed lost selection
and was fixed before final acceptance. No failed run is counted as a pass.

Candidate executable SHA256 ab0ca93b8d7aebf9921d0a5bf14f51db48a15fd6f1495ed6bd21778688c80ecc. It was promoted after the user confirmed ordinary Native Exit, under the
existing compatible profile lock and runtime exclusive lock. No automatic
real-profile launch, payload read, profile write, backup or rollback image.
Real visual confirmation remains pending.

The compact private post-Exit delta does not add pending maps to the preceding
baseline. It does expose loss of DONE flags inside maps already mixing DONE and
RECHECK: Native preparation rejects the entire mixed map and stages all pieces
as RECHECK. Original RDResumeHandler preserves DONE independently of pending
pieces. Historical progress counters stayed unchanged. This is an additional
resume compatibility observation, not a UI fix or evidence of deleted payload.
It remains open; reproduce mixed maps and queued retirement before changing the
write path. No real identifiers, captures or personal configuration are in this
record. Next UI proposal is the original-style status bar, after prioritising
this resume preservation issue. No new chapter was implemented.
