Adopt the three staged files after exact before-SHA verification. No build/run
was performed. Same existing scheduler-service target/module fixture, no CMake
change. The added seventh family retains all six previous families and assertions.

Host sequence for move/relink only:
1. Capture actual old retained admission and current manualRevision before stop.
2. Commit storage mutation after certified retirement/forget.
3. Re-admit with manuallyPaused=true when that was the actual/persisted pause;
   stop_when_ready allows verification but never payload before manual release.
4. Obtain the actual new retained/manualPaused admission, then call
   rebindOwnedPause(lease,hash,oldToken,newToken,sameManualRevision).
   A false result grants nothing (legacy/user pause); an error keeps it paused
   and surfaces a diagnostic. Do not revoke or mint scheduler ownership.

The method authenticates the lease and exact unchanged config/XML/ownership/
intent sources, rejects pending/uncertain actions, and transfers only an existing
acknowledged pair. Original PausedTorrents.conf is never written. Successful
transfer increments revision and invalidates all issued action/rate plans.
Removal continues to revoke ownership and clear approved peer hints; move/relink
must retain the owner's approved numeric hints. No broader scheduler policy
change or original Java scheduler equality is claimed.
