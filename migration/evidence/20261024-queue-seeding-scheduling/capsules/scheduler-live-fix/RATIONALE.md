# Scheduler directory compatibility correction

Actual GCC r2 runtime failures all entered scheduler_detail::directory before module/service use. The actual runner inherits umask0002 (read-only shell inspection); std::filesystem/QDir-created original-style plugin data directories are therefore0775. The service incorrectly required (mode&0022)==0 for data directories, rejecting correct owner/live/nofollow paths. This is not a traversal or descriptor-move failure.

Change only that data-directory mask to0002: owned group-writable directories are allowed; world-writable directories remain unsafe. Regular-file UID/single-link checks, all bounded reads and named/fd identity/CAS checks remain unchanged. The independently authenticated executable module still rejects group OR world write bits via0022; no module guard is relaxed.

The existing jar-only resolution family now explicitly constructs0775 parents so the compatibility assertion remains meaningful under any CI umask. Existing unsafe family adds explicit0777 refusal. No methods, assertions or deadlines are removed. Six source families remain; no new target or execution.

Inputs are copied from actual sibling native tree after root batch. Actual shared file_io.hpp:17 and file_io.cpp provide move assignment; earlier movable-fd proposal is unnecessary and is NOT included. No native changes/build/test/process rerun/Java/network by this author. Root can adopt two files then rerun its affected batch.
