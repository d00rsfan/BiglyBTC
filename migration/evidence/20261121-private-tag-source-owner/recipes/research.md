# Private source witness and SDK-before-storage integration

D8 separates a worker-confined source controller from passive paused constructor
bundles. Its source-reading preparation rejects held outer source gates;install
and constructor callbacks need no source access. Dropping an unbound bundle
cancels the reservation;completed targets stay in the actual binding census.

Private ProfileSource currently checks noPrivateTagRates before ManagedStorage.
It decodes an intact primary only during payload admission,then socket turns
check passive tag.config/bak/saving stamps. ManualTagSnapshot::sameSource uses
captured preimage equality across stores;it explicitly is not an edit grant.
Generic PreparedTagRateSource must retain exact actual ManualTagStore lineage.

Introduce a SEPARATE sealed private source witness:actual shared SDK cohort,
profile ownership,current complete source,canonical/full target and the actual
future private SDK session. A dropped/stale pending bundle,foreign SDK/profile,
backup/saving source,changed membership/limits or expired group must refuse.
The witness retains no profile lease or source gate. It grants no generic CAS.
Consumption must bind the actual OwnedProfile session before storage and still
require the complete paused constructor proof before first resume.

OwnedProfile SDK creation already precedes payload storage outside the gate.
prepareTorrent currently holds the gate across prepareChild and prepareStorage.
A controller callback must run in a distinct unlocked phase after child/source
capture,followed by exact config/child revalidation under the gate immediately
before storage. Never call ProfileTagRates from a waiting private turn while
SourcePermit holds its independent source gate.

Startup permission alone is incomplete:live source writes also need refreshed
private passive tag stamps/witnesses and actual Host all-role effects. Keep the
active-rate fence until these paths qualify;do not implicitly add storage
permission to the D8 SDK-only bundle. The global/LAN common owner remains open.
