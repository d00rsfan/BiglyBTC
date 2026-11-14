# Source-bound chat routes retain canonical identity

Native `b90051f8e9bca07a7168921df63672c1bb3c1791`, tree `2fdd461735bdf59083d71522634c76d9b4170362`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.

RoomSession can renew finite handler-specific DATA leases and move pending Calls
to a same-IP physical port under the current fixed room epoch. Participant contact
exports retain the advertised external address and actual protocol version; the
full signed private identity and encrypted channel remain unchanged. Queued sends
count towards the six-operation bound and expire; ordinary admitted peers keep
the existing single-preflight send path. The optional asynchronous tunnel hook
runs before ordinary sync without holding a permit over crypto or network waits.
It is not yet attached by the application owner in this source commit.

Actual SDK IPv4/IPv6 cases passed: public canonical-contact propagation to a third
actor, and complete private STS followed by encrypted delivery after a physical
port move. Payload plaintext is absent from the wire, stale room epochs cannot
renew routes and untrusted hints do not grant DHT admission. The first private
run passed authentication, message and route assertions but its final withdrawal
assertion exposed a test adapter using a changing epoch rather than the fixed
room-token epoch used by the application. Only that adapter was corrected. The
two private cases then passed in0.74s; four retained shared public/private entries
passed in1.66s. The final fixture-only build took24.84s after the grouped80.49s
build. Existing public cases were retained without repeating them.

These are owned loopback/header-seam tests on the frozen SDK, GCC16/C++23/Qt6.12,
not real NAT acceptance or full Java runtime interoperability. Application child
source activation, directory/punch consumers, automatic own relay publication,
reachability and graceful withdrawal remain open. No live image/profile changed.
