# Profile pending census cohort log correction

Implementation remains `ad63fce04e5ba493c4510f139acffe167d1f6572`,tree
`4f7af432f3705bc69236d040a64fae0e4709663a`. No production/test source changed.
The prior evidence remains immutable at
`migration/evidence/20261121-profile-tag-preadd`.

Its Native spec used `profile-pending-native-cohort-final` for both the existing
Qt cohort executable and the new plain pending-cohort mode. Both ran exit0,
but the latter overwrote the former log. Thus the archived tally199 counted
that plain log twice and omitted10 Qt rows. This is an evidence identity bug.

Only the existing Qt cohort test was rerun under the unique
`profile-pending-native-existing-cohort-final` identity:10 passed,0 failed,
0 skipped. The corrected full spec has130 distinct task identities and209 Qt
rows. All other final logs retain their already qualified hashes. The original
199-row record is preserved;this new record supplies the corrected manifest.
Both the reusable runner and publisher now reject duplicate task identities
before execution/publication. Updated helpers and the exact rerun log are
retained here. SDK/daily/profile selection and all private/global limits stay
as recorded in the original bounded review. Continue private integration.
