# P04-09 Saved selected columns

State: implemented and locally verified in native
`03e31e5ce260e1327e63cca63d22d8d5984ceae9`. All34 units pass with zero skips;
26 new Qt rows also pass leak-enabled ASan/UBSan. Actual three-view/chooser smoke
and four captures pass100/150/200%. Hosted publication and dist promotion are
recorded separately in the verification receipt.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native pure prerequisite `aaa7a1f4a84b66c0edc3d900c701e38a81fcfd2b`.
Use the frozen P04-08 source/math contracts and exact integration audit pins.

Root scope decision before implementation:

- Append logical columns10–12, preserving all0–9, current Name sort, existing
  full Size/Progress/CompletionRole/state/action/details behavior. Label them
  Saved selected size (bytes), Saved selected remaining (bytes), Saved selected done.
- All three new columns hidden by default in both panes, available through the
  existing chooser. Reset Columns hides them again. No new persistence format.
- Host query projects once from current row.cached->bytes, RAW saved.completedBytes
  and row.cached->savedDnd. Never use Host::known fallback, prior UI bytes or
  sanitized completed as saved-metric inputs. No per-torrent IO or extra worker.
- Append optional SavedSelectedValues to LibraryItem, containing optional qint64
  bytes/remaining, optional qint32 percentThousandths and copied quint32
  defaults/diagnostics. Absence means no current cached source. Do not export
  controller booleans as pane policy. Each new snapshot replaces these values;
  telemetry and selected-row details leave them unchanged.
- Missing metrics use em dash and invalid numeric sort role. Bytes use raw signed64
  admitted nonnegative values. Done Display/Accessible uses exact one decimal
  from integer permille; numeric sort uses integer permille, no double math.
- Model rejects negative saved byte/remaining values, remaining greater than known
  selected bytes or permille outside0..1000 before mutating the previous snapshot.
  No second DND calculation in Qt. Unknown optionals remain independent.
- Header/cell tooltip explains a saved snapshot of selected files and unknown
  unavailable/inconsistent values in user language, without exposing raw masks.
- Metadata-download suppression, assumedComplete/pane policy, cache fixup/per-file
  recovery and original live column equivalence remain OPEN. These are explicitly
  labelled saved primitives, not a claim of original full Size/Done semantics.

Separate production/test ownership. Root owns CMake, legacy count assertions,
preview fixtures/smoke, independent checks, evidence, commit/push and dist staging.
Qualification: meaningful Qt role/sort/chooser/refresh/lifecycle fixtures plus a
real generated-profile Bridge/cache end-to-end read/immutability check, full native
units, focused sanitizer check, actual three-view smoke/captures. No normal profile,
network sockets, original JVM initialization or process interruption.

Evidence and exact14-file source manifest:
`migration/evidence/20261018-saved-selected/verification.json`.
Independent production review7c0dce2c found no blockers and reproduced allfour
beforeimages byte-identically after inverting the listed changes. Root preserved
initial harness/count failures and their exact corrections in the verification
history; no production behavior was changed to make those expectations pass.

Hosted desktop39119435708/SDK39119435750 both pass. Downloaded artifacts
independently confirm34units/26newQtrows/zero skips and real smoke/four captures.
Dist is promoted with103 verified files and82 byte-identical runtime files;
full previous dist retained. No process signaled or normal profile opened.

Public metadata0e96f6a5d8f6922791ab0655dc93b13e163366af is tagged
`milestone-classic-saved-selected-20261018`; remote main and peeled annotated tag
were checked against that exact hash. The next source-only original layout
checkpoint is [P04-10](P04-10-classic-column-layout-projection.md).
