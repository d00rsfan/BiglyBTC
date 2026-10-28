# P06-06 Original local rating load/store policy

Status: bounded host policy qualified and published. Move beyond payload
codecs to the original RatingsUpdater.loadRatingsFromDownload and
storeRatingsToDownload behavior at1629340340d0bc391d9e582f9ee989a93d550581.
Use supplied immutable host attribute values/nickname; return staged attribute
write intentions and a publication-queue intention without touching files or
network. Keep operational DDB, actual host attribute loading/writing, timers and
Qt editor integration pending until their shared services are qualified.

Allowed paths: native/plugins/BiglyBTC-plugin-azrating local-policy files/tests,
task-local source oracle/fixtures/evidence and build consumers. Do not change
qualified rating data semantics. Preserve missing comment default, upper-only
score cap, signed32 Integer.parseInt behavior, Unicode digit handling, setter
order, nickname-only publication and null/partial failure behavior. A user action
can stage a publication request but this model must never authorize actual DDB
publication, bypass host eligibility/settings or silently mutate metadata.

Pin original method bodies, original RatingData class and Java17 reference
semantics before implementation. Characterize normal/boundary/failure and UTF-16
cases with a guarded helper, including observations of setters queued before a
later error. Compare GCC16, Clang24 and ASan/UBSan/LSan; retain existing rating
fixtures/readbacks. This is a bounded host-policy slice, not completion of the
rating plugin. Record exact immutable source/native hashes after qualification.

70000 original observations pass per compiler, including every BMP UTF-16 code
unit (65536),576 additional load cases and3888 ordered store cases. Original
parseInt exceptions are captured through a supplied helper-only stderr stream;
the model reports their presence while preserving the original zero fallback.
37 decimal digit ranges pin qualified Java17 behavior without a runtime Java/ICU
dependency. Nickname-only publication, null comment setter/removal and partial
setter intentions before null-nickname failure are retained. The non-null
replacement RatingData contract and1Mi UTF-16-unit per-string native bound are
explicit. All four rating CTest rows and366 original Java payload readbacks pass
GCC16, Clang24 and ASan/UBSan/LSan, using hash-verified installed core packages.

Local implementation:97921ec837ef7394d7be83ec5b39119eceea1453.
Public source update:019c13d8916285351441f69e14cf001116a9cb58.
All25 committed export files match provenance; a fresh GCC export build passes
all four test rows against separately installed public core sources. Source,
publication and independent-build receipts are retained. No operational plugin
or binary release was enabled. Continue P06-07 message-sync STS compatibility.
