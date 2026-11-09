# Shared Public UDP bridge for the LT provider

This additive overlay provides Azureus packet ingress/egress through the actual
libtorrent torrent UDP listeners. It is a lower-layer dependency of general
Public chat rooms. It does not activate rooms or replace runtime approval.

Apply `shared-udp.patch` to the previously qualified provider source, not a clean
upstream release. `source-delta.json` identifies the three required preimages,
three added files, resulting file digests and complete overlay digest. The
upstream SDK baseline is `740a0b9aeabe00e762cc0efe4a0f27593db2550b`;
the parent includes the project's earlier provider patches. Refuse other
preimages or existing added files before applying the patch.

With that source as the working directory:

```sh
patch --dry-run --batch -p1 < shared-udp.patch
patch --batch -p1 < shared-udp.patch
cmake --build ../build --target test_biglybtc_public_udp --parallel 2
LD_LIBRARY_PATH=../build ../build/test/test_biglybtc_public_udp -n
```

Reuse the existing SDK CMake cache/toolchain with tests enabled. The test target
is discovered by the SDK's existing test-file glob on CMake regeneration. The
checks use owned loopback listeners, synthetic payloads and an owned UDP tracker.
They require IPv4 and IPv6 loopback support. They do not require public routing,
root network setup, an actual torrent profile or a real chat room.

The API adds five versioned exports and keeps the parent provider's42 exports.
Attach returns actual local listener addresses/ports and current external-address
observations. Send selects the exact local address and checks route suitability;
it cannot silently choose another same-family socket. The receive callback names
its local binding. Bindings can be refreshed without rebinding.

Calls post to the SDK executor and acknowledge within a bounded operation budget.
The caller must retain its source permission through acknowledgement. Each physical
send also checks the observer's current atomic permission. A queued operation
cancelled at its deadline cannot act later. Callbacks run on the SDK executor;
they must only copy to a bounded mailbox and must not block, allocate, throw,
retain packet pointers or call the SDK. The native room worker/mailbox is later
integration work.

uTP has dispatch priority. Valid BEP5 and UDP-tracker traffic retains its original
path; the observer receives only candidate Azureus framing. Candidates still
require the actor's full parsing, correlation, network and source checks. Direct
transport and the actual SDK IP filter apply. The bridge creates no additional
sockets and changes no torrent/uTP settings.

Do not install this candidate over the existing qualified runtime. A future app
build needs matching updated headers (including internal session layout), a
separate candidate artifact and explicit derived runtime qualification. The
existing user's launch build remains pinned to its prior approved provider.
