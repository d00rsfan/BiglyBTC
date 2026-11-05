# Held I2P contacts and classic manual tag editing

Native implementation `f859404f63394add41c898d40e486499d3a45178`, source tree `342bb186f44e10531938c0f55ad403bdd35f6a86`; parent `3bf6e0aeb26bc9c32b896c4f35a23bac1970e329`.
Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` and I2P helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078` remain the immutable behaviour baselines.

I2P routing now restores compatible contacts as unknown, performs bounded maintenance under the held source, and exports contacts through the existing worker. The original dat/saving layout, source/identity guards and joined shutdown are retained. Lookup identifiers no longer stop after 1,024 cancelled requests. Source snapshots do not authorise network work.

The classic Tools tag manager and selected My Torrents tag menu create, rename, remove, show and assign manual download tags through the real session worker. Raw unknown fields and unresolved members are retained. Edits check the same profile lease and original file preimage; uncertain writes require explicit reload and never replay the failed edit. UI results are correlated by session, request, generation and selected target.

Each compiler lane built the desktop, both probes and the affected tests: **32 CTests passed on GCC; the same 32 passed with leak-enabled ASan and UBSan**. The complete Native source map was frozen before and after each lane and associated with the implementation commit. The server-interrupted SAN attempt and duplicate-cache cancellation were retained; only SAN was resumed. GCC successes were not replayed. Compilation warnings were fixed without changing routing logic, and a direct test include dependency was declared.

The [actual offscreen tag manager capture](manual-tags.png) uses an owning generated presentation fixture. It establishes widget rendering, not a complete desktop/profile editing run. Detailed test names, executable digests, input pins, source map digest and raw receipt digests are in verification.json. Raw logs remain in the persistent migration build workspace.

Whole P07/P08 remain in progress. Real-router inbound completion still needs a new ordinary-terminal authentication after binding the corrected probe; the already successful selected magnet cases remain valid historical evidence. Original Java contact-file round trips, normal startup UI editing, automatic tag rules/actions and the remaining library features are follow-ups. The running Java client and its live profile were not touched, the original index is unchanged, and no default preview distribution was promoted.
