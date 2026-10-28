# Original local rating host policy review

Original azrating baseline:1629340340d0bc391d9e582f9ee989a93d550581.
The exact loadRatingsFromDownload/storeRatingsToDownload method bodies execute
with supplied Download attributes/setter recorder, Plugin.getNick and publication
recorder. The independently qualified unchanged RatingData class supplies score
clamping and nullable UTF-16 fields. No updater constructor, timers, plugin/core
startup, actual attribute I/O, DDB or network runs. Original parse exceptions
write to a helper-only captured stderr stream; observations report their presence.

The native pure API owns loaded values and ordered intentions. Missing score is
zero without an error; invalid Integer.parseInt text is zero with a diagnostic.
Missing comments become empty. Signed32 boundaries and upper-only score cap are
preserved. Java17 char-based decimal parsing is exhaustively pinned across all
65536 code units, including37 BMP digit ranges, NUL, surrogate halves and other
numeric categories. Additional load cases cover signs, whitespace, overflow,
mixed-script digits, supplementary digit pairs and nullable/unpaired-surrogate
text. No runtime Java/ICU dependency or floating-point parser is introduced.

Store previews retain original setter order: rating string, comment (possibly
null), then urgent publication. A nickname-only change has no attribute setters.
Null old nickname can throw after setter calls; the preview retains those
intentions and marks failure before publication. The preview never mutates caller
inputs or applies/queues anything. It requires a non-null replacement RatingData;
host getter/setter exceptions, target identity/revision binding, actual attribute
loading/writing, durable partial execution/recovery and complete publication
eligibility remain future host work. Queue intention alone does not authorize
DDB publication. Cached global averages and full updater scheduling remain out of
this bounded component.

Native inputs/replacement strings each have a1Mi UTF-16-unit bound, rejected
before intentions. This explicit resource limit does not assert arbitrary Java
string acceptance. Tests check limit boundaries, immutable caller values and
owned plans surviving input mutation/destruction/moves.

70000 original observations (65536 code units,576 other loads,3888 stores) pass
GCC16, Clang24 and ASan/UBSan/LSan. All four rating CTests and366 original Java
payload readbacks pass per compiler. Installed dependency prefix artifacts match
the independently qualified core package hashes. Compiler/source/class/fixture/
binary hashes and full outputs are retained. No leaks are suppressed. Review by
the implementing agent only; no independent reviewer was used.
