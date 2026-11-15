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

Pure eligibility/query batching prerequisite qualified in `24cec5ff4b3c65fa80815cf185a10d0a3ca7881a`; [review](../evidence/20261115-azureus-survey-eligibility/REVIEW.md). Continue actual survey integration.

Owning topology/repair decisions qualified in `11bf8e16a4bd74fd1246a6a4b55e371c08084343`; [review](../evidence/20261115-azureus-survey-topology/REVIEW.md). Continue contact-state/DB identity and real runtime integration.

Contact state/DB incarnation prerequisite qualified in `e0e216d6b40832923a751fb2d220cc63cce47ca6`; [review](../evidence/20261115-azureus-survey-state/REVIEW.md). Continue bounded candidates and actual state/runtime integration.

Bounded owning survey candidates qualified in `fec5460eab04dc7d447c6d7aef74b099bafb50aa`; [review](../evidence/20261115-azureus-survey-candidates/REVIEW.md). Continue feature-flag activation and actual state/runtime integration.
