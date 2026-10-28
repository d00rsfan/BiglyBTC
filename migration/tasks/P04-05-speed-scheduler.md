# Speed Scheduler original policy and XML

State in_progress; owner Codex; self review only. Native project name
BiglyBTC-plugin-SpeedScheduler; runtime ID SpeedScheduler, display Speed Scheduler.
Original baseline `1ee6a53dae017696d2f0002e4a0d74f5a9476af5` from
https://github.com/BiglySoftware/BiglyBT-plugin-SpeedScheduler.git.

Port Time/Schedule matching, overlap rates, category/tag membership, force-start
exceptions, default-rate restoration and ownership of paused downloads into an
explicit deterministic tick. Import the existing SavedSchedules.xml unchanged;
retain unknown content and its original bytes. PausedTorrents.conf ownership and
application policy integration must be qualified before enabling the feature.

The full original plugin compiles from its frozen tree against the source-built
core/UI and retained SWT compile artifact under /tmp. Never initialize its normal
UI/plugin entry point. Use bounded host value proxies for original XML/time tests.
If a clock/one-tick test overlay is needed, retain its exact patch and SHA; never
call an instrumented scheduler an unmodified operational plugin baseline.

Known source semantics to characterize: Time.between accepts the closed interval
between either endpoint, even when end precedes start (it does not wrap midnight);
overlaps choose the largest numeric rate, so zero unlimited does not dominate a
positive rate; category exclusions can apply across overlapping pause rules.
Do not silently replace those rules with intuitive alternatives. UI/editor,
actual engine pause/resume and AutoSpeed routing are distinct pending gates.
The source tree does not include a top-level license grant: establish release
license provenance before distribution. Development code is not a release claim.

GCC implementation/unit and four frozen original XML/model/time fixture sets pass.
Unmodified original XML/Time/Schedule methods match 151200 minute comparisons,
including UTF16, legacy pause flags, unknown XML and reversed/equal endpoints.
The unchanged original Thread.run and real pause-file helper methods also match
11 scenarios x576 host-value states =6336 comparisons: commands, rates, selected
schedules and resulting ownership. No source clock or loop overlay was needed.
A fixture-only Error from the next sleep-setting query stops the second tick;
an isolated empty original core supplies TagManager; host values and external
AutoSpeed/rate routing are substituted explicitly. See P04-05/gcc-original-ticks.json. Actual native ownership-file writes, engine application, tags/AutoSpeed
routing and editor remain required. Compiler/sanitizer plugin matrix remains next.

The later ownership-file codec was qualified on GCC/Clang and sanitizers (P04-06).
It uses the
original uppercase four-byte-group formatter, Java ASCII trim and case-sensitive
membership, CR/LF/CRLF line parsing, first-only duplicate removal, byte/line
bounds and indexed lookups. Unknown raw lines/encodings are retained on removal.
An append after an unterminated foreign line is explicitly rejected: original
Java merges the new hash into that line and cannot subsequently find ownership.
Native APIs return edited bytes; physical ownership-file writer and runtime host
activation remain pending. The five policy/XML/tick/ownership suites pass; the
later P02-05 shared module adds a sixth original-command replay suite through the
SDK. See P02-05 compiler capsules for the exact current implementation and scope.
