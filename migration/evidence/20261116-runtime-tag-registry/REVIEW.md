# Shared runtime tag metadata and guarded positive witnesses

Native source `e2bad7506b7b3a877f9a8da53e5db9e3f543a454`, tree `c93b6713fd72dcb11471751ed1fb840ad97622e3`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

A bounded immutable core registry owns supplied type/tag registration order,
UIDs, resolved/unknown names, taggable-type flags, groups and membership. Original
name lookup selects the first match in each type, retaining type registration
order. Membership queries union all matching tags and use exact taggable-type
equality, not a bit-mask overlap. UTF16 names retain exact case and Unicode
normalisation distinctions. Unknown earlier names can prevent certifying the
first match; unknown later names cannot supersede it. Partial type/name/member
censuses cannot certify absence. A known positive witness remains true despite
other unknown entries. Identity, text, entry and allocation bounds fail without
truncating the source into a false negative.

An isolated original helper calls actual TagBase name/UID, TagTypeBase name/
membership and TagManagerImpl lookup/union methods on generated records. It
supplies lightweight objects through isolated allocation and suppresses Core
singleton initialisation; peer flag metadata uses an interface wrapper while
name/UID/member methods delegate to the actual original object. This is metadata
API evidence, not a running Java tagger, peer-set service or network. Two type
orders,24 distinct name queries and6 membership queries cover duplicate names,
localised source labels, unknown-key fallback, exact UTF16 spelling and equality
of DOWNLOAD2/PEER4 versus combined6. Native replays72 name comparisons across the
three subject types and six membership results. Earlier public-overload/Core
setup and incorrect generated peer-flag failures are excluded.

The expression evaluator accepts this owning registry alongside existing scalar
facts, counts registry resources against its own quotas and keeps unavailable
names/members unavailable. The normal profile collector supplies only actual
ordinary manual membership; its source census is deliberately incomplete.
Saved byte order is retained as metadata and is not claimed to reconstruct the
whole live registration order. Unresolved localisation, constrained membership,
other tag types and unsafe group effects remain unknown.

One known ordinary positive tag is no longer hidden by an unresolved same-name
constrained alias. This follows original membership union semantics rather than
confusing it with first-per-type name lookup. Negative/absent results still do
not become false under the incomplete native census. Current-source checks bind
registry data to the same profile writer, exact tag preimage, subject and worker
turn. Reload, equal-byte writer reopen, foreign profile, wrong turn and unsafe
group tests keep existing membership. Metadata can be reused for the same
subject/turn/preimage; selected-own age is removed and derived afresh for every
rule, preventing another rule's timestamp from surviving into an absent tag.
The registry's owning shared data avoids rebuilding its metadata for every rule
on the same captured row. No benchmark improvement is claimed.

Actual normal worker tests cover positive dependency Add and logically justified
negative removals through Store/CAS, unresolved aliases/cycles/localisation,
source removal retaining unknown dependencies, supported/unsupported rule modes,
stale/foreign/cancelled/closed turns and unsafe group refusal. Existing opaque
fields remain preserved. One coherent GCC16 Release/C++23/Qt6.12.0 application
link and focused core/evaluator/profile/worker groups pass. Unrelated unchanged
DHT, statistics and controls suites were not replayed. No live profile, payload,
running client, current daily or protected SDK was modified.

This qualifies the shared metadata/ordinary-positive integration milestone.
Full category/state/internal/swarm/peer-set source censuses, named age/count/
position and recursive constrained dependency rechecks remain in the current
P08 chapter. No full registry/F13 or upstream integration baseline is advanced.
Continue those independent tasks without stopping or preparing a new image.
