# Torrent properties ready for review

Resize fix `b9db322e088d3f8e75b1aec256e077b9e8b401e5` and core properties `4afe96eca3d08799b65e0ff341df5d2cf5de9862` are qualified.
See [review](evidence/20261112-torrent-properties/REVIEW.md) and
[task](tasks/P06-TORRENT-DETAILS-PROPOSAL.md). All15 core views have native content;
advanced Java/plugin parity remains explicitly recorded in the review.

No next chapter has started. Next: swarm merging, with ordinary priority.
IP-filter remains low; embedded browser/media player remain skipped.

The user is running the prior frozen preview. Do not interrupt it or modify the
real profile. The new candidate is built, not launched/promoted. Normal Exit is
needed to release its runtime lock. Then compare compact original state metadata
with the private baseline (both clean-to-pending and lost DONE inside pending
maps), update the frozen preview and retain the same canonical launch command.
Do not create full backups or rollback images. Local continuation and candidate:
`build/torrent-properties-20261112/WORK.md` / `candidate.json` in the original repo.
The baseline lives in `build/resume-statusbar-20261112/private/` and must stay private.
