# Original recursive tag dependency control protocol

Native source `92084d41f4326a9cde0fae4af2a85dc26ac0a1aa`, tree `3ac7289d6839124d8fbed93a71ff8ea3651588d7`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

An isolated helper invokes actual TagConstraint.applySupport2/testConstraint and
inherited TagWithState membership methods. Actual CopyOnWriteSet snapshots back
the recorded tag effects. Generated expression/effect boundaries supply controlled
truth and record mutations; no original parser, live automatic tagger, file or
network service is newly qualified. The earlier mutable-set draft was replaced
before acceptance because it did not preserve the original root set snapshot.

The137 cases compare full evaluation/Add/Remove order, final membership and checked
path state across root/dependency modes, existing membership, new-only dispatch,
chains, cycles, recheck after an Add changes truth, disabled dependency, prechecked
root/child, disabled dependency checks, recent-add refusal and stop during an
expression. The original method passes the same initial root membership snapshot
to subdependencies. This reference behavior is recorded explicitly; it must not
be replaced by a guessed topological ordering or silently treated as each child's
own existing snapshot.

A pure bounded native explicit-stack protocol reproduces those traces. It asks
an owner for evaluation and effect outcomes without owning a callback, IO, clock,
profile, writer or permission. Distinct sequence tokens reject stale replies when
the same rule is re-evaluated. Missing evaluation is terminally unavailable,
failed effects do not change model membership, duplicate edges and bounds refuse
without new effects, and checked paths terminate cycles. Node/edge/depth/step caps
are lower-only. Source/session/provider/CAS authorization remains outside this
value protocol and cannot be created by its requests.

GCC16 Release/C++23/Qt6.12.0 focused build and original-order/adversarial protocol
checks pass. This core model has no production consumer yet; application linking
will occur once at coherent worker integration. Current profile behavior and
existing conservative unknown constrained-tag facts are unchanged. Real guarded
recursive execution, source-bound transient membership overlays and bounded
single-publication integration remain current work. No live profile, payload,
running client, protected SDK, daily build or full registry/F13 baseline changed.
Continue the current chapter autonomously.
