# Original signed message flags and attention predicates

Nativea7205853ea382c69e12e881eb48442cb48b8d367, tree
735caa3c945a29ba9b798b097439e43242947274. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Source/body hashes are in
[source-scope.json](source-scope.json). This is the first primitive/metadata
milestone of P06-CHAT-DISPLAY-ATTENTION, not completed profile/UI filtering.

Original getFlagFlashOverride/getFlagType and existing getFlagOrigin numeric
rules preserve Java integer narrowing, missing defaults and legacy merge-origin
handling. Core attention differs from display visibility: local messages require
flash override, remote messages require not ignored, and the actual branches
hide rating origin1/subscription origin3. Origin4 is not excluded by those
branches. Normal decoded-text display additionally rejects empty/ignored text.
The predicates intentionally preserve these specific source decisions.

7168 comparisons execute the exact selected original flag methods, Core
attention if/else branches and SWT visibility prefix. In-memory supplied payload,
participant/preferences and a null optional own-address are explicit
collaborators; network/profile/SWT lifecycle is not executed. The optional Core
own-address update is outside this predicate's native scope. The configured
original-chat-attention-policy test passes in0.62seconds, zero skips.

Authenticated room messages now retain f.f/f.t alongside f.o, without rewriting
raw protocol/history data or adding new outgoing fields. An actual encrypted
signed-message fixture verifies independent flash/type values and long-to-int
narrowing, original history handoff, and refusal of a modified flag without a
new signature. The fixture initially omitted its contact map and was correctly
refused; the generated fixture was fixed, with no production admission relaxation.
The existing512-identity/bounded128-message room regression also passes in
2.00seconds. Older desktop service binaries are not acceptance evidence for
this appended metadata layout; consumers will rebuild for the integrated
profile/UI milestone. No running preview, user profile or SDK changed.

Global display preferences, participant properties/controls and read-marker
handoff remain active work. No browser/player or non-Public parity is implied.
