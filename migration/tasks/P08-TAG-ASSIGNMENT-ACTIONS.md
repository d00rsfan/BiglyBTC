# P08 actions on tag assignment

Original baseline selected before implementation in the retained exact-source
oracle manifest: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.

Port Start/Resume, Stop/Queue/Pause and Force/NotForce assignment actions, preserving
state/metadata/destroyed guards, branch priority, dispatch order and execution-time
flag reads. Exact extracted original branch uses recording async/DM boundaries;
it does not certify the whole addTaggable, scheduler or real command ownership.

Next connect strict saved pp.<action> flag reading and actual assignment receipts
to the existing normal serialized Host/ManualTagStore/engine command owners.
No startup/load/reload replay. Only a real committed fresh membership change may
create a dispatch receipt; no-op, failed, cancelled, recovered, stale or foreign
sources must not create effects. Existing generic store remains strict until an
actual actor handles these actions. Preserve unknown/unsupported scripts, hosting,
file moves, option templates and nested-tag actions without guessing effects.
Explicit command execution must leave the filesystem gate before engine calls.

The pure lifecycle protocol is not an executor, writer grant or whole F13 finish.
Continue autonomous plan work; no new daily, real-profile access or client launch.
