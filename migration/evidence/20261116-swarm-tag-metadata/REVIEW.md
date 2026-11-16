# Saved Swarm Tag metadata without torrent membership

Native source `5b93396804d1ad10fff75180b69a696464456e9b`, tree `615d305ef34f44bd4313ccbd245c36f46307ae7f`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Actual TagSwarmTagImpl reports TT_NONE0 and deliberately refuses add/remove
membership. Its saved-map constructor has no resolver, so even declared saved
o/p entries do not create actual download or peer membership. An isolated helper
calls the actual map constructor, inherited membership methods, name/UID methods
and no-op add/remove implementations with the manager disabled and no Core.
Thirty-two identity/name/saved-member combinations and96 subject attempts confirm
those boundaries, including IDs0/1/17/Integer.MAX_VALUE and localized/raw names.

The normal source-bound collector now includes saved type7 metadata from the
actual owning tag document. Canonical IDs/UIDs, literal UTF16 names and supported
group metadata are retained. TT_NONE/absent membership prevents saved objects from
becoming a download/peer witness. Unresolved tag.* localization stays unknown.
Original loadTags ignores non-digit type attributes such as c; those remain
byte-exact and are not parsed as tag IDs. Malformed/aliased numeric IDs or type
shapes withhold facts without rewriting the source. Entry/text/global registry
bounds remain enforced. Persisted declarations do not certify later live creation,
last-seen events or the full original type order; the census stays partial.

Actual isolated Store/fact tests replay the32 identity/name forms and96 membership
attempts, verify literal metadata lookup and compile-time tag-name existence,
retain type-level opaque data, reject malformed types/aliases and confirm no
backup/pending file is created. Earlier source/immutable-plan/age/lineage checks
remain green. The first test used invalid original Boolean syntax (bare true
inside a compound expression); a tiny core-only probe identified it, and the
accepted test uses (1==1). That preliminary failure is excluded. Normal state,
category/manual dependency and stale/foreign/cancelled/closed consumer checks pass,
as does one GCC16 Release/C++23/Qt6.12.0 application link. No sanitizer or full
original Swarm service/network run is newly claimed.

No live profile, payload, running client, protected SDK, daily launcher or rollback
image changed. Actual internal/peer source ownership, ordinary constrained-source
membership, new-only recursive execution and named metrics remain current P08
work. No whole registry/F13/global integration baseline advances. Continue without
pausing or preparing a new daily. Agent self-review; no independent reviewer.
