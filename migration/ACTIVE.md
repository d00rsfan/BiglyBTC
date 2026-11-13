# Current work — exit defect only, then STOP

The user reported a frozen shutdown and authorised diagnosis/metadata capture.
Follow [tasks/P11-LARGE-LIBRARY-CLOSE.md](tasks/P11-LARGE-LIBRARY-CLOSE.md).
The old Native has exited; compact private metadata is saved. The user may run
Java now. Do not access its live profile or launch Native. Fix only this close
path, qualify the replacement and stop; distributed keyword/Bloom and all other
chapters remain deferred. Keep the next-launch baseline fresh after Java Exit.
