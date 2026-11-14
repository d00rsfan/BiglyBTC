# Original rendezvous server binding policy

Native `3f7b61f6ca2175fb525407ff4a7664703ec79329`, tree `a812d7193af08e5333a95f33e7b67c29f6e9b87b`. Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Bindings now reproduces the original eight-entry server limit, secondary refusal,
observed-port reply, healthy rebind retaining the original saved contact and
counters, strict greater-than five-minute expiry, and healthy-if-failures<8 OR
strictly more than30s since the last connect result. Success clears consecutive
failures. Maintenance is explicit, matching the original timer's role; bind and
candidate lookup do not eagerly expire entries between maintenance events.

The memory policy returns contacts/leases and never sends packets or admits a
peer. Unique serials prevent a completion for a removed/replaced/cleared entry
from changing its replacement. The eventual actor still validates source epoch,
actual endpoint, network/lane, receipt role/deadline and once-only completion.
Overflow is saturated/refused rather than allowing Java counter wrap. These are
explicit native lifetime/bounds protections, not original wire extensions.

Four QTest entries passed in0.37s: native cap/rebind/failure/strict-time/secondary/
stale-lease cases and75 commands compared with exact original receiveBind,
BindingData and the server expiry block of the original perform callback. A
controlled monotonic clock, memory monitor/contact/transport interfaces and
CT_UDP1 are the projection seams. Secondary-rendezvous de-duplication following
expiry in that same Java callback is deliberately excluded and remains open.
The100 prior wire-builder examples and codec checks passed again in0.27s after
extending their shared oracle. [Qualification](qualification.json),
[exact source scope](source-scope.json).

Whole NAT/private chapter remains open. Client selection/initial and keepalive
bind failures, publication lookup, direct observed-port authority, tunnel retries,
three-party actual shared-UDP owner and chat integration are next. No real
NAT/router/peer, profile or running-preview change is claimed or performed.
