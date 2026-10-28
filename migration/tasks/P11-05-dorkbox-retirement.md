# Accepted dorkbox cleanup retirement

State: retired by user scope decision, 2026-10-17. Owner/reviewer Codex, self review
only. Decision: [AD-007](../decisions/AD-007.md).

Original source baseline:
`83da70fdac15cf9f0f1372e25956c28a88c39f51` in
https://github.com/BiglySoftware/BiglyBT-plugin-dorkboxupdater.git.
Reviewed `DorkBoxUpdaterPlugin.initialize`, `startUpgrade`, `UIAttached` and
`showScriptAutoUpdateDialog`: Java installation cleanup and restart prompting.

No native implementation, replacement repository, release asset or automatic
cleanup is required. Keep the original audit/source record and Java files/settings.
Native tray and the separate authenticated native updater remain required work.
No Java plugin/process was launched, no profile was opened and no runtime code
changed. Registry/docs validation is sufficient for this accepted scope change;
future native catalogue/discovery must recognise the retired ID without repair.

Saved native scope-decision integration commit:
`bb09972151c929c7e2a66f0951d135e8f7862e1f` on the isolated review branch.
`first_ported_commit` and `integrated_through_commit` remain null because
retirement is not a source port or an integration-baseline advance.
