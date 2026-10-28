# P06-25 Owned FIND_NODE authority and STORE admission

Status: implemented bounded generated service; fresh GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P06-25-owned-authority/{matrix.json,review.md}. Baseline chosen before isolated drafting:
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Combine qualified P21 Authority and
P22 Admission in the existing generated-only Qt UDP owner. Exact original
normal-contact admission/FIND_NODE/STORE/controller gates are pinned by source
audits and a new independently frozen Java bridge. No static snapshot cookie
may authorize a STORE.

An explicit immutable generated service configuration is exclusive with inert
server replies. Bind a new numeric-loopback endpoint, derive its local ID and
create authority/admission on the actual owner; failure rolls back before Bound.
Same-IP/different advertised port is allowed; mismatched advertised IP returns
ERROR1 with observed transport endpoint before control/auth work. Identity uses
advertised endpoint and originatorVersion. Empty FIND_NODE target still issues
the real shared authority's IP token; existing fixed20-byte APIs remain intact.

Admission alone decides when to call the actual authority verifier. Empty,
mismatched and unseeded forwarded requests must preserve source cold-cache gates.
Use supplied key/routing/seed snapshots; unknown metadata grants no ownership.
Keep source event order, nullable diversification/block behavior and unsendable
partial output. No real DB/router/expiry/persistence/plugin activation is implied.
Bounded owned receipts retain requests referenced by policy event indices;
reserve capacity before service effects, preserving moved/copy lifetime.

Source-only1070 observations (452 STOREs/110 FN queries/452 serializers) were
frozen before native comparison. Real socket qualification targets20 protocol/
snapshot contexts,210 requests each direction,840 datagrams and210 owned
receipt/gate comparisons per compiler. Java uses two fresh connected numeric
loopback sockets for ordinary/raw advertised-header probes; unchanged fence
allows exactly the two new native ports. Source shadow predictions run actual
original verification with their own issued tokens; only opaque token fields
are normalized across independent ephemeral authority keys. Do not claim a
full native1070-row replay from those targeted socket comparisons.

Native Qt rows cover IPv4/IPv6, cold gates/invalid tokens/metadata/block/null,
identity/provider failure, startup rollback, receipt capacity/lifetime and owner
movement. Repeat all prior packet, authority/admission and owned UDP regressions
on fresh persistent GCC16/Clang24/ASan+UBSan+LSan builds, at most two compilers.
Preserve failures, immutable helper/core/runtime/source/binary hashes and actual
Git provenance; original Java HEAD/index/running program/live profile untouched.
Continue real DHT storage/lookup/persistence and dependent host/plugin work.

Native implementation: `294c21279540306997a9f293eeef0d768f9dda74`. All246 qualified inputs and one recovery
source match its immutable Git blobs; see committed-inputs.json. No full-client
release verification or upstream integration baseline advances.
