Extend the existing initial-session component, without activating an I2P backend.

The pure SessionConfig codec signs Destination || sorted UTF-8 properties ||
big-endian8-byte date; the raw DSA0/Ed7 signature follows those fields. Java
UTF-16 key order, duplicate refusal,255 encoded-byte strings and65535-byte
properties body are explicit. Online Enc0 identities only; offline and auth
options remain Unsupported. Client-only filtering follows the exact13-element
producer list; overly long Java strings are removed before wire-byte checks.
Create1 wraps config; Reconfigure2 prefixes an unsigned16-bit ID outside the
signature. The existing shared OpenSSL signer/verifier is reused via two bounded
entry points; old datagram/identity algorithms remain unchanged.

Channel registration is explicit after the date, never automatic. It accepts a
borrowed ClientIdentity only synchronously for signing, retains public destination
and options only, and requires one request at a time with bounded write backlog.
The caller supplies creation milliseconds explicitly. Per-attempt freshness is
checked against the observed router date plus monotonic elapsed time; the raw
date is not installed as a global clock. This explicit timestamp contract is a
bounded prerequisite, not original automatic Clock initialization parity.

A matching current-socket CREATED grants SessionCreated(id) only. Real IDs are
0..65534;65535 is UNKNOWN. No LeaseSet, tunnels, Connected/ready, lookup, payload,
profile or backend authority is granted. Reconfigure requires the same exact
public Destination and accepted ID, signs a fresh config, and commits putAll
options only on matching UPDATED. Omission does not remove an old option.
The original router verifies create signature/date (±180s), but deliberately
does not reverify reconfigure signature/date; native still sends correct signed
configs and does not mislabel UPDATED as router cryptographic verification.
No request nonce exists in this wire format: timeout/cancel/loss closes the
connection and generation before reuse. Unknown next messages, including future
LeaseSet requests, remain explicit Unsupported until their separate handlers
are implemented; this is not an operational whole-router client yet.

Source basis is9134 plus plugin33d context. Two exact router source blobs were
retrieved and matched the retained Git tree (router-source-pins.json).
SessionConfig.java230–361; DataHelper.java189–219/290–320;
I2CPMessageProducer.java68–74/111–168/427–451;
ClientMessageEventListener.java226–264/774–816;
ClientManager.java92–97/436–447;
I2PSessionImpl.java801–834 (waits for LeaseSet before becoming usable).

Meaningful affected checks: existing pure datagram Java fixtures and client
identity key ownership/interop units, new DSA0/Ed7 signed-config tests, existing
frame/initial-channel tests extended with actual owned Create/Updated/refusal,
wrong-ID/signer/generation/cancel/deadline/reentry controls. An offline Java
helper will use actual SessionConfig/Create/Reconfigure objects against both
pinned JARs, comparing preimages/parsed fields and cross-verifying signatures,
not random signature equality. No compiler, Java, router or test execution is
authorized for the author while the current cohort runs.
