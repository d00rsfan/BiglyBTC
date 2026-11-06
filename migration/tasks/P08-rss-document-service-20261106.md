# P08 RSS document service and remaining consumers

State: in_progress. The bounded parse service is qualified; fetch/search/subscriptions and remaining providers need separate source ownership, quotas, cancellation/joins and actual acceptance. Whole-plan autonomous authorization remains active.

## 2026-11-06 bounded RSS document service

Implementation `1cf7cb5ae92c0098bab2476a313c352b13e022a9`, Native subtree `266cf68746e58b172c798b07e42fd663a09c60a3`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: reusable owning RSS/Atom parse service integrated at seven leaves, actual three-TU front-end checks and one GCC/SAN CTest per lane passed (eight current Native families; two ELF images plus archive dependency). [Evidence](../evidence/20261106-rss-document-service/REVIEW.md). The child withholds conditional magnet-derived hashes until link selection settles, preserving independent explicit hashes. Prior r2 actual20-command/nine-group RSSFeed/RSSItem/WebResult value proof remains carried parent-only, not rerun. No app, fetch/network/HEAD/cache/search/subscription/date interpretation/cancellation-provider lifecycle or whole P08/global/default-distribution claim follows. Continue the authorized whole plan with separately reviewed source-owned fetch/consumer architecture; earlier tag/history and P07 milestones remain retained.

