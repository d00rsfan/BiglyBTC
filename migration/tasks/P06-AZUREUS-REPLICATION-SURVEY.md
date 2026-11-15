# Azureus explicit replication survey

Dependent on qualified cache maintenance and query-storage. Original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Port actual DHTDBImpl.applyRF,
survey/doQuery/queriesComplete and SurveyContactState, with actual source-value
replication nibble semantics. First characterise pure eligibility/timer decisions
using actual original methods and explicitly controlled generated JVM clocks and
RNG. Keep monotonic and wall clocks distinct; preserve Java overflow/remainder,
first-observation deferral, frequency-zero behaviour and diversified-key refusal.

Then integrate owning DB candidates, current-source UDP queries, peer state,
copy counts/closest-node selection and original subnet/failed-contact handling.
No normal-cache fallback for explicit-only values. Do not infer network authority
from a planner result. Retain endpoint/source/receipt/deadline bounds and private
publication fences. Do not claim F09 complete while surveys or signed key blocks
remain open. No intermediate daily or live-profile changes; use focused checks
and one coherent application link at integration completion.
